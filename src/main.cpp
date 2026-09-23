// mxl-multi-compositor
//
// Reads N MXL flows directly from the MXL domain (tmpfs) via libmxl (zero-copy, no
// per-flow decode pass), lays them out in a single GStreamer compositor
// element, encodes the mosaic once with x264, and pushes the result to
// mediamtx via RTSP. Bypasses mediamtx's per-flow encoder backlog so the
// wall-clocks burned in by the writers (mxl-gst-testsrc's clockoverlay
// element) actually agree across tiles at composite time.
//
// Flow list comes from the MXL_FLOW_IDS env var (space-separated UUIDs), one
// tile per flow. MXL_TILES adds tiles that start empty, for a controller to
// connect over NMOS (see src/nmos/node.hpp).
// Output destination from MXL_COMPOSITE_OUT (default rtsp://mediamtx:8554/composite).
// MXL domain from MXL_DOMAIN (default /domain).
//
// Each flow runs a worker thread that drives a dedicated appsrc with v210
// grains. The compositor element does the layout in I420 space; encoding
// happens once at the output, not per-flow.

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "nmos/node.hpp"
#include "nmos/slots.hpp"

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

namespace
{
    std::atomic<bool> g_exit{false};

    // Per-flow source dimensions. The MXL flow config exposes grain rate and
    // slice sizes but not width/height, so the tile resolution is taken from
    // MXL_FRAME_WIDTH / MXL_FRAME_HEIGHT (set to match the producer's flow
    // descriptor). Defaults to 1080p for backwards compatibility.
    int g_frameW = 1920;
    int g_frameH = 1080;

    // Mosaic geometry + per-grain payload size, set in main() once the flow
    // count is known. Read by the /stats.json server thread so the frontend
    // can report the RDMA-delivered throughput per tile.
    int g_cols = 1;
    int g_rows = 1;
    // Output canvas, set in main() once the grid is known. Native tiles (no
    // downscale) => g_outW/g_outH = grid * source frame size.
    int g_outW = 1280;
    int g_outH = 720;
    std::int64_t g_grainBytes = 0;

    void on_signal(int) { g_exit.store(true, std::memory_order_relaxed); }

    std::string env_or(char const* key, char const* fallback)
    {
        char const* v = std::getenv(key);
        return v ? std::string{v} : std::string{fallback};
    }

    std::vector<std::string> split_ws(std::string const& s)
    {
        std::vector<std::string> out;
        std::istringstream is{s};
        std::string tok;
        while (is >> tok) out.push_back(tok);
        return out;
    }

    // Minimal extractor for a top-level integer field in the flow-def JSON,
    // e.g. "frame_width": 1920. Avoids pulling in a JSON dependency for two
    // numbers. Matches the exact quoted key so it won't collide with the
    // nested "width"/"height" under components[]. Returns -1 if absent.
    int json_int_field(std::string const& json, char const* field)
    {
        auto const key = std::string{"\""} + field + "\"";
        auto pos = json.find(key);
        if (pos == std::string::npos) return -1;
        pos = json.find(':', pos + key.size());
        if (pos == std::string::npos) return -1;
        ++pos;
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
        int val = 0;
        bool any = false;
        while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9')
        {
            val = val * 10 + (json[pos] - '0');
            ++pos;
            any = true;
        }
        return any ? val : -1;
    }

    // Re-implementation of the Cursor pattern from mxl-gst-sink. Keeps each
    // reader pinned to the live grain index for its flow's rate; next()
    // sleeps until the next grain's delivery deadline so workers don't busy-
    // poll. readDelay=0 because the compositor wants the freshest frame.
    struct Cursor
    {
        mxlRational rate;
        std::uint32_t windowSize;
        std::int64_t readDelayGrains;
        std::uint64_t currentIndex;
        std::uint64_t requestedIndex;
        std::uint64_t deliveryDeadline;

        Cursor(mxlRational r, std::uint32_t w, std::int64_t readDelayNs)
            : rate{r}
            , windowSize{w}
            , readDelayGrains{((::mxlTimestampToIndex(&r, readDelayNs) + w - 1) / w) * w}
        {
            realign(::mxlGetTime());
        }

        void realign(std::uint64_t now)
        {
            currentIndex = ((::mxlTimestampToIndex(&rate, now) + (windowSize / 2)) / windowSize) * windowSize;
            requestedIndex = currentIndex - readDelayGrains;
            deliveryDeadline = ::mxlIndexToTimestamp(&rate, currentIndex + windowSize);
        }

        void next()
        {
            ::mxlSleepUntil(deliveryDeadline);
            currentIndex += windowSize;
            requestedIndex += windowSize;
            deliveryDeadline = ::mxlIndexToTimestamp(&rate, currentIndex + windowSize);
        }
    };

    // How many grains behind the live head to read. The newest grain may be
    // mid-write; lagging a couple keeps every tile on a grain the writer has
    // actually finished producing.
    constexpr std::int64_t kLagGrains = 2;

    struct FlowWorker
    {
        std::size_t index;
        // The flow this worker has open, or is opening. Owned by the worker
        // thread; what the tile should show is slots->get(index), which the
        // worker follows.
        std::string flowId;
        nmos_slots::Slots* slots{nullptr};
        std::string domain;
        ::mxlInstance instance{nullptr};
        ::mxlFlowReader reader{nullptr};
        ::mxlFlowConfigInfo config{};
        GstElement* appsrc{nullptr};
        std::thread thread;
        std::atomic<std::uint64_t> framesPushed{0};
        std::atomic<std::uint64_t> framesMissed{0};
        // Delivered grain rate, refreshed by the STATS ticker every 5s.
        // This is the rate at which RDMA-mirrored grains land in the
        // consumer's domain -- the per-flow RDMA delivery quality.
        std::atomic<double> fps{0.0};
    };

    // v210 packs 6 pixels into 16 bytes; the row stride is rounded up to a
    // 48-pixel / 128-byte boundary. Per-grain payload = stride * height.
    std::int64_t v210GrainBytes(int w, int h)
    {
        std::int64_t stride = static_cast<std::int64_t>((w + 47) / 48) * 128;
        return stride * h;
    }

    // black_v210 is one v210 frame of video black: luma 64, chroma 512, packed
    // three 10-bit samples to a little-endian 32-bit word in the order
    // Cb Y Cr, Y Cb Y, Cr Y Cb, Y Cr Y.
    std::vector<std::uint8_t> black_v210(int w, int h)
    {
        std::int64_t const stride = static_cast<std::int64_t>((w + 47) / 48) * 128;
        std::vector<std::uint8_t> out(static_cast<std::size_t>(stride * h));
        auto word = [](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
            return a | (b << 10) | (c << 20);
        };
        std::uint32_t const words[4] = {
            word(512, 64, 512), word(64, 512, 64), word(512, 64, 512), word(64, 512, 64)};
        for (int y = 0; y < h; ++y)
        {
            auto* row = out.data() + static_cast<std::size_t>(stride) * y;
            for (std::int64_t off = 0; off + 4 <= stride; off += 4)
            {
                std::uint32_t v = words[(off / 4) % 4];
                row[off] = v & 0xff;
                row[off + 1] = (v >> 8) & 0xff;
                row[off + 2] = (v >> 16) & 0xff;
                row[off + 3] = (v >> 24) & 0xff;
            }
        }
        return out;
    }

    // Minimal single-threaded HTTP/1.0 responder for /stats.json. No deps --
    // the compositor already links gst/glib but a raw socket keeps the
    // surface tiny. Serves the live per-flow delivery metrics. CORS-open so a
    // browser page served from another origin can poll it.
    void stats_http_server(std::vector<FlowWorker>* workers, int port)
    {
        int srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0) { g_printerr("stats: socket() failed\n"); return; }
        int one = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<std::uint16_t>(port));
        if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            g_printerr("stats: bind(:%d) failed\n", port);
            ::close(srv);
            return;
        }
        if (::listen(srv, 16) < 0) { g_printerr("stats: listen() failed\n"); ::close(srv); return; }
        // Bound accept() so the loop can observe g_exit and shut down even
        // when no client is connecting.
        timeval tv{};
        tv.tv_sec = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        g_print("stats: serving /stats.json on :%d\n", port);

        while (!g_exit.load(std::memory_order_relaxed))
        {
            int cli = ::accept(srv, nullptr, nullptr);
            if (cli < 0) { if (g_exit.load(std::memory_order_relaxed)) break; continue; }

            // Drain (and ignore) the request line/headers.
            char buf[2048];
            ::recv(cli, buf, sizeof(buf), 0);

            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
            std::ostringstream body;
            body << "{\"ts\":" << now
                 << ",\"provider\":\"verbs\""
                 << ",\"outW\":" << g_outW << ",\"outH\":" << g_outH
                 << ",\"cols\":" << g_cols << ",\"rows\":" << g_rows
                 << ",\"grainBytes\":" << g_grainBytes
                 << ",\"flows\":[";
            for (std::size_t i = 0; i < workers->size(); ++i)
            {
                auto& w = (*workers)[i];
                double fps = w.fps.load(std::memory_order_relaxed);
                double mbps = fps * static_cast<double>(g_grainBytes) * 8.0 / 1.0e6;
                if (i) body << ',';
                body << "{\"i\":" << i
                     << ",\"flowId\":\"" << w.slots->get(i) << "\""
                     << ",\"label\":\"MXL-" << (i + 1) << "\""
                     << ",\"fps\":" << fps
                     << ",\"pushed\":" << w.framesPushed.load()
                     << ",\"missed\":" << w.framesMissed.load()
                     << ",\"mbps\":" << mbps
                     << ",\"live\":" << (fps > 1.0 ? "true" : "false")
                     << "}";
            }
            body << "]}";
            std::string json = body.str();

            std::ostringstream resp;
            resp << "HTTP/1.0 200 OK\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Cache-Control: no-store\r\n"
                 << "Content-Length: " << json.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << json;
            std::string out = resp.str();
            ::send(cli, out.data(), out.size(), 0);
            ::close(cli);
        }
        ::close(srv);
    }

    void worker_loop(FlowWorker* w)
    {
        using clock = std::chrono::steady_clock;
        auto rate = w->config.common.grainRate;
        // A tile that starts with nothing connected has no flow to take a rate
        // from; it runs at the rate the mosaic is encoded at.
        if (rate.numerator <= 0)
        {
            rate.numerator = 30000;
            rate.denominator = 1001;
        }

        // Drive the read loop off a monotonic clock at the flow's grain rate.
        // The producers (mxl-gst-testsrc) free-run at ~5x realtime, so each
        // flow head runs ahead of nominal cadence. Walking the ring in sequence
        // forced the compositor to copy, decode, and scale every grain the
        // writers produced (~5x the 30 fps they actually emit). That pinned it
        // at 6-7 of 9 tiles and starved the rest to 0 fps; the realign churn on
        // the starved tiles also flickered the mosaic. Now we sample the newest
        // finished grain (head - kLagGrains) once per grain period and skip the
        // rest, so read/decode load stays at N x 30 fps regardless of writer
        // speed. Re-reading the head each tick also leaves no cursor to wedge
        // when a flow's index jumps or it restarts.
        auto const period = std::chrono::nanoseconds{
            rate.numerator > 0
                ? static_cast<std::int64_t>(1'000'000'000LL) * rate.denominator / rate.numerator
                : 33'366'700LL};

        g_print("[%zu] %s starting at rate %d/%d (paced %lld ns/grain)\n",
            w->index, w->flowId.c_str(),
            rate.numerator, rate.denominator,
            static_cast<long long>(period.count()));

        // Push the appsrc caps once before the first buffer. The capsfilter
        // downstream in the pipeline asserts the format, but appsrc needs an
        // explicit set so videoconvert can negotiate without a stall.
        //
        // Width/height MUST match the flow's real frame size: the worker
        // pushes a buffer of the flow's actual grainSize, so declaring the
        // wrong dimensions makes gst read V210 with the wrong stride ->
        // torn/partial frames (flicker). mxlFlowConfigInfo carries no
        // dimensions, so read them from the flow def. Producers that don't
        // rescale (e.g. the SRT bridge) publish whatever resolution they
        // ingest, so this can't be assumed to be 1080p.
        int vw = g_frameW;
        int vh = g_frameH;
        if (!w->flowId.empty())
        {
            std::vector<char> defBuf(16384);
            std::size_t defSize = defBuf.size();
            if (::mxlGetFlowDef(w->instance, w->flowId.c_str(), defBuf.data(), &defSize) == MXL_STATUS_OK)
            {
                std::string const def{defBuf.data()};
                int const pw = json_int_field(def, "frame_width");
                int const ph = json_int_field(def, "frame_height");
                if (pw > 0 && ph > 0)
                {
                    vw = pw;
                    vh = ph;
                }
                else
                {
                    g_printerr("[%zu] %s: frame_width/height absent in flow def; defaulting %dx%d\n",
                        w->index, w->flowId.c_str(), vw, vh);
                }
            }
            else
            {
                g_printerr("[%zu] %s: mxlGetFlowDef failed; defaulting caps %dx%d\n",
                    w->index, w->flowId.c_str(), vw, vh);
            }
        }
        g_print("[%zu] %s caps %dx%d v210 @ %d/%d\n",
            w->index, w->flowId.c_str(), vw, vh, rate.numerator, rate.denominator);

        auto* caps = ::gst_caps_new_simple("video/x-raw",
            "format", G_TYPE_STRING, "v210",
            "width", G_TYPE_INT, vw,
            "height", G_TYPE_INT, vh,
            "framerate", GST_TYPE_FRACTION, rate.numerator, rate.denominator,
            nullptr);
        ::g_object_set(G_OBJECT(w->appsrc), "caps", caps, nullptr);
        ::gst_caps_unref(caps);

        // Staleness watchdog. A mirror can go silent -- writer churn, mirror
        // re-form, a gateway roll leaving a half-open RDMA endpoint -- while its
        // ring stays readable: the head index simply stops advancing and reads
        // keep returning the same stale grain forever, with no error to key
        // off. Track the head; if it freezes for kStallReopen ticks (~2s at the
        // grain rate), drop the reader so the guard at the loop top re-attaches
        // it to the live ring. This is what insulates the compositor from
        // upstream churn without an external restart.
        std::uint64_t lastHead = 0;
        int stallTicks = 0;
        constexpr int kStallReopen = 60;
        // Highest grain index already pushed downstream. The scan below picks
        // the freshest COMPLETE grain, but when head-kLagGrains is briefly
        // mid-write it falls back to an older grain -- which, versus the
        // previous tick, would rewind the burned-in clock by a frame. Never
        // emit a grain older than the last one shown: hold the last frame
        // instead. Reset on reopen (a recreated flow restarts its indices).
        std::int64_t lastShownIndex = -1;

        auto nextTick = clock::now();
        auto pace = [&]() {
            nextTick += period;
            auto now = clock::now();
            if (nextTick < now) nextTick = now + period;  // fell behind: don't burst
            else std::this_thread::sleep_until(nextTick);
        };

        auto const black = black_v210(vw, vh);
        auto push = [&](std::uint8_t const* data, std::size_t size) {
            auto* buf = ::gst_buffer_new_allocate(nullptr, size, nullptr);
            GstMapInfo map{};
            ::gst_buffer_map(buf, &map, GST_MAP_WRITE);
            std::memcpy(map.data, data, size);
            ::gst_buffer_unmap(buf, &map);
            GST_BUFFER_DURATION(buf) =
                ::gst_util_uint64_scale_int(GST_SECOND, rate.denominator, rate.numerator);
            GstFlowReturn ret = GST_FLOW_OK;
            ::g_signal_emit_by_name(w->appsrc, "push-buffer", buf, &ret);
            ::gst_buffer_unref(buf);
            return ret;
        };
        // A flow whose frame size is not the tile's. The tile is composited
        // at its native size with no scaler, so such a flow is refused and the
        // tile stays black until something else is connected.
        std::string refused;

        while (!g_exit.load(std::memory_order_relaxed))
        {
            // Follow the tile's slot. A controller connecting another flow
            // over NMOS changes it; the reader is dropped here and the guard
            // below opens the new one.
            if (auto want = w->slots->get(w->index); want != w->flowId)
            {
                if (w->reader != nullptr)
                {
                    ::mxlReleaseFlowReader(w->instance, w->reader);
                    w->reader = nullptr;
                }
                g_print("[%zu] tile now %s (was %s)\n", w->index,
                    want.empty() ? "(none)" : want.c_str(),
                    w->flowId.empty() ? "(none)" : w->flowId.c_str());
                w->flowId = std::move(want);
                lastHead = 0;
                stallTicks = 0;
                lastShownIndex = -1;
            }

            if (w->flowId.empty() || w->flowId == refused)
            {
                auto ret = push(black.data(), black.size());
                if (ret != GST_FLOW_OK && ret != GST_FLOW_FLUSHING) break;
                pace();
                continue;
            }

            // Re-open a reader dropped by the watchdog or the FLOW_INVALID path
            // below, once the flow is published again. Keeps reopen logic in one
            // place so both stalls and hard tear-downs recover identically.
            if (w->reader == nullptr)
            {
                bool active = false;
                if (::mxlIsFlowActive(w->instance, w->flowId.c_str(), &active) == MXL_STATUS_OK && active &&
                    ::mxlCreateFlowReader(w->instance, w->flowId.c_str(), "", &w->reader) == MXL_STATUS_OK)
                {
                    ::mxlFlowReaderGetConfigInfo(w->reader, &w->config);
                    lastHead = 0;
                    stallTicks = 0;
                    lastShownIndex = -1;
                    g_print("[%zu] %s reader re-opened\n", w->index, w->flowId.c_str());

                    std::vector<char> defBuf(16384);
                    std::size_t defSize = defBuf.size();
                    if (::mxlGetFlowDef(w->instance, w->flowId.c_str(), defBuf.data(), &defSize) == MXL_STATUS_OK)
                    {
                        std::string const def{defBuf.data()};
                        int const fw = json_int_field(def, "frame_width");
                        int const fh = json_int_field(def, "frame_height");
                        if (fw > 0 && fh > 0 && (fw != vw || fh != vh))
                        {
                            g_printerr("[%zu] %s is %dx%d, the tile is %dx%d; not shown\n",
                                w->index, w->flowId.c_str(), fw, fh, vw, vh);
                            ::mxlReleaseFlowReader(w->instance, w->reader);
                            w->reader = nullptr;
                            refused = w->flowId;
                            continue;
                        }
                    }
                }
                else
                {
                    pace();  // flow not back yet; hold cadence and retry
                    continue;
                }
            }

            // Resolve the freshest finished grain from the live head each tick,
            // rather than advancing a cursor -- decouples our cadence from the
            // writer's (over)production rate and self-heals index jumps.
            ::mxlFlowRuntimeInfo rt{};
            bool haveIndex = ::mxlFlowReaderGetRuntimeInfo(w->reader, &rt) == MXL_STATUS_OK &&
                static_cast<std::int64_t>(rt.headIndex) > kLagGrains;

            // Head not advancing? Count consecutive frozen ticks and drop the
            // reader once it's clearly wedged; a healthy flow resets the count.
            if (haveIndex && rt.headIndex == lastHead)
            {
                if (++stallTicks >= kStallReopen)
                {
                    g_print("[%zu] %s head frozen %d ticks -- reopening reader\n",
                        w->index, w->flowId.c_str(), stallTicks);
                    ::mxlReleaseFlowReader(w->instance, w->reader);
                    w->reader = nullptr;
                    stallTicks = 0;
                    pace();
                    continue;
                }
            }
            else if (haveIndex)
            {
                lastHead = rt.headIndex;
                stallTicks = 0;
            }

            // Take the freshest grain, scanning from the live head downward,
            // and key everything off the grain's OWN index (info.index), not
            // the index we asked for. info.index is "the epoch grain index the
            // ring-buffer slot currently holds" -- so it exposes a STALE read:
            // mxl-gst-testsrc constantly backfills the grain grid, and a
            // skipped slot still holds the frame from one ring-depth ago. If we
            // probe such a slot, GetGrain returns it but info.index is the OLD
            // index, not what we requested. The previous code trusted the
            // requested index for ordering, so it would show that old frame
            // under a "newer" label and the burned clock flapped back. By
            // ordering on info.index instead (and holding when it isn't newer
            // than the last shown), the actual CONTENT advances monotonically --
            // which is exactly the invariant the gateway's sequential transfer
            // gives the cross-node mirrors. Accept the INVALID flag (every
            // local-ring grain carries it); only skip a half-written grain.
            constexpr int kScanDepth = 5;
            ::mxlGrainInfo info{};
            std::uint8_t* payload = nullptr;
            bool got = false;
            bool readerDead = false;
            std::int64_t chosenIdx = -1;
            int lastRet = 0;
            ::mxlGrainInfo lastInfo{};
            for (int back = 0; haveIndex && back < kScanDepth; ++back)
            {
                std::int64_t idx = static_cast<std::int64_t>(rt.headIndex) - back;
                if (idx < 0) break;
                auto ret = ::mxlFlowReaderGetGrain(w->reader,
                    static_cast<std::uint64_t>(idx), 100'000'000ULL, &info, &payload);
                lastRet = static_cast<int>(ret);
                if (ret != MXL_STATUS_OK)
                {
                    if (ret == MXL_ERR_FLOW_INVALID) { readerDead = true; break; }
                    continue;  // aged out / not ready -- try one below
                }
                lastInfo = info;
                if (info.validSlices < info.totalSlices) continue;  // mid-write, try older
                chosenIdx = static_cast<std::int64_t>(info.index);  // the slot's ACTUAL content index
                got = true;
                break;
            }

            if (!got)
            {
                auto missed = w->framesMissed.fetch_add(1, std::memory_order_relaxed) + 1;
                if (missed % 150 == 1)  // ~once / 5s: diagnose why nothing is readable
                    g_print("[%zu] %s no complete grain: head=%llu lastRet=%d "
                            "valid=%u total=%u flags=0x%x size=%zu\n",
                        w->index, w->flowId.c_str(),
                        static_cast<unsigned long long>(rt.headIndex), lastRet,
                        lastInfo.validSlices, lastInfo.totalSlices,
                        static_cast<unsigned>(lastInfo.flags),
                        static_cast<std::size_t>(lastInfo.grainSize));
                if (readerDead && w->reader != nullptr)
                {
                    // Flow torn down / recreated and the handle is dead. Drop
                    // it; the reopen guard at the loop top re-attaches.
                    ::mxlReleaseFlowReader(w->instance, w->reader);
                    w->reader = nullptr;
                }
                pace();  // hold cadence even while a flow is down
                continue;
            }

            // Never rewind the CONTENT. chosenIdx is the grain's own index
            // (info.index), so a stale slot -- one whose index is older than
            // what we already showed -- is held here instead of displayed,
            // keeping the burned-in clock monotonic. Clean mirror flows always
            // have info.index == head, so they advance every tick.
            if (chosenIdx <= lastShownIndex)
            {
                pace();
                continue;
            }
            lastShownIndex = chosenIdx;

            // Zero-copy would mean handing the payload pointer to gst with a
            // custom GstAllocator that knows libmxl's ring buffer lifetime;
            // copy is sufficient for v1 -- payload at 1080p v210 is ~5 MB and
            // gst's slab allocator absorbs it cheaply.
            auto* buf = ::gst_buffer_new_allocate(nullptr, info.grainSize, nullptr);
            GstMapInfo map{};
            ::gst_buffer_map(buf, &map, GST_MAP_WRITE);
            std::memcpy(map.data, payload, info.grainSize);
            ::gst_buffer_unmap(buf, &map);

            // Don't set PTS -- appsrc has do-timestamp=true so gst stamps
            // each buffer with the pipeline running clock on push. mxl's
            // timestamps are a monotonic ns counter from a different
            // epoch, and compositor (which uses pipeline running-time as
            // its reference) would queue everything as "future" and never
            // mix it onto its canvas. Keep duration so the compositor can
            // estimate cadence.
            GST_BUFFER_DURATION(buf) =
                ::gst_util_uint64_scale_int(GST_SECOND, rate.denominator, rate.numerator);

            GstFlowReturn pushRet = GST_FLOW_OK;
            ::g_signal_emit_by_name(w->appsrc, "push-buffer", buf, &pushRet);
            ::gst_buffer_unref(buf);

            if (pushRet != GST_FLOW_OK && pushRet != GST_FLOW_FLUSHING)
            {
                g_printerr("[%zu] push-buffer returned %d, exiting worker\n",
                    w->index, static_cast<int>(pushRet));
                break;
            }

            w->framesPushed.fetch_add(1, std::memory_order_relaxed);
            pace();
        }

        g_print("[%zu] %s worker exiting (pushed=%llu missed=%llu)\n",
            w->index, w->flowId.c_str(),
            static_cast<unsigned long long>(w->framesPushed.load()),
            static_cast<unsigned long long>(w->framesMissed.load()));
    }
}

int main(int argc, char** argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    ::gst_init(&argc, &argv);

    auto domain = env_or("MXL_DOMAIN", "/domain");
    auto outUrl = env_or("MXL_COMPOSITE_OUT", "rtsp://mediamtx:8554/composite");
    auto flowIdsStr = env_or("MXL_FLOW_IDS", "");
    g_frameW = std::atoi(env_or("MXL_FRAME_WIDTH", "1920").c_str());
    g_frameH = std::atoi(env_or("MXL_FRAME_HEIGHT", "1080").c_str());
    if (g_frameW <= 0 || g_frameH <= 0) { g_frameW = 1920; g_frameH = 1080; }
    g_print("Source tile resolution: %dx%d\n", g_frameW, g_frameH);
    auto flowIds = split_ws(flowIdsStr);

    // MXL_TILES is the tile count where it exceeds the flows named: the
    // extra tiles start empty and show black until a controller connects a
    // flow to them over NMOS.
    {
        int const tiles = std::atoi(env_or("MXL_TILES", "0").c_str());
        if (tiles > 0 && static_cast<std::size_t>(tiles) > flowIds.size())
            flowIds.resize(static_cast<std::size_t>(tiles));
    }
    if (flowIds.empty())
    {
        g_printerr("no tiles -- set MXL_FLOW_IDS to a space-separated list of UUIDs, "
                   "or MXL_TILES to a count to connect over NMOS\n");
        return 2;
    }
    if (flowIds.size() > 16)
    {
        g_printerr("Only the first 16 flows fit the grid; ignoring overflow\n");
        flowIds.resize(16);
    }

    // Square-ish grid sized to the flow count: cols = ceil(sqrt(n)),
    // rows = ceil(n / cols). 1->1x1, 2->2x1, 4->2x2, 9->3x3. Output stays
    // 1280x720; tiles are OUT/cols x OUT/rows so they tile it exactly when
    // the dimensions divide (4 flows -> 2x2 of 640x360).
    // Column count defaults to a near-square grid; MXL_GRID_COLS overrides it
    // (MXL_GRID_COLS=1 stacks the tiles in a single vertical column).
    {
        std::string const colsEnv = env_or("MXL_GRID_COLS", "");
        int const envCols = colsEnv.empty() ? 0 : std::atoi(colsEnv.c_str());
        g_cols = envCols > 0
            ? envCols
            : static_cast<int>(std::ceil(std::sqrt(static_cast<double>(flowIds.size()))));
    }
    if (g_cols < 1) g_cols = 1;
    g_rows = static_cast<int>((flowIds.size() + g_cols - 1) / g_cols);
    if (g_rows < 1) g_rows = 1;
    // No downscale: each tile is the full source frame, the canvas is the
    // grid times the source size. High-detail patterns (circular, checkers,
    // zone-plate) alias into solid-colour garbage when a 1080p tile is
    // squeezed to a few hundred px + v210 chroma packing; at native res they
    // render clean. Cost is a large output (e.g. 6x 1080p -> 5760x2160) -- the
    // RDMA fabric carries it, but note an h264 decoder width cap of 4096 can
    // stop browsers playing canvases wider than two 1080p columns.
    int const TILE_W = g_frameW;
    int const TILE_H = g_frameH;
    g_outW = g_cols * TILE_W;
    g_outH = g_rows * TILE_H;
    int const OUT_W = g_outW;
    int const OUT_H = g_outH;
    // Scale the encoder bitrate with the canvas area so a 4K+ mosaic isn't
    // starved at the 720p-tuned 6 Mbps. Linear in pixels off the 1280x720
    // baseline; the fabric has the bandwidth.
    long long const baseline = 1280LL * 720LL;
    int bitrateKbps = static_cast<int>(
        6000LL * static_cast<long long>(OUT_W) * OUT_H / baseline);
    if (bitrateKbps < 6000) bitrateKbps = 6000;

    // The fabric has the bandwidth, but the path to a viewer may not. Where a
    // network refuses UDP, WebRTC media reaches a browser through a relay over
    // TCP, and a relay forwards what that connection can carry and discards the
    // rest. What arrives is then a fraction of every frame, so no frame ever
    // completes: bytes accumulate at the viewer while the picture stays black.
    //
    // A cap is the cluster's to set, because it is a property of the delivery
    // path rather than of the mosaic. Unset changes nothing.
    int const capKbps = std::atoi(env_or("COMPOSITOR_MAX_BITRATE_KBPS", "0").c_str());
    if (capKbps > 0 && bitrateKbps > capKbps) bitrateKbps = capKbps;
    g_grainBytes = v210GrainBytes(g_frameW, g_frameH);
    g_print("Grid: %zu flows -> %dx%d, tile %dx%d, out %dx%d, bitrate %dkbps, grainBytes %lld\n",
        flowIds.size(), g_cols, g_rows, TILE_W, TILE_H, OUT_W, OUT_H, bitrateKbps,
        static_cast<long long>(g_grainBytes));

    // Build the pipeline as a single gst-launch-style description. compositor
    // has per-sink xpos/ypos/width/height set via pad properties; we write
    // them inline here so the result is one parse_launch call (no manual pad
    // linking).
    // Bounded leaky queues are mandatory at every fan-in/fan-out point.
    // Without them gst's default unbounded queueing piles up buffers when
    // any downstream element stalls (rtspclientsink handshake, encoder
    // catch-up after pipeline state change) and the pod OOM-kills inside
    // seconds -- what was killing the 3 Gi / 8 Gi attempts.
    std::string pipelineDesc =
        "compositor name=comp background=black ";
    for (std::size_t i = 0; i < flowIds.size(); ++i)
    {
        int row = static_cast<int>(i) / g_cols;
        int col = static_cast<int>(i) % g_cols;
        pipelineDesc += "sink_" + std::to_string(i) +
            "::xpos=" + std::to_string(col * TILE_W) +
            " sink_" + std::to_string(i) +
            "::ypos=" + std::to_string(row * TILE_H) +
            " sink_" + std::to_string(i) +
            "::width=" + std::to_string(TILE_W) +
            " sink_" + std::to_string(i) +
            "::height=" + std::to_string(TILE_H) + " ";
    }
    pipelineDesc +=
        "! video/x-raw,format=I420,width=" + std::to_string(OUT_W) +
        ",height=" + std::to_string(OUT_H) +
        ",framerate=30000/1001 "
        // Drop-old leaky queue right after the compositor so a brief
        // encoder hiccup doesn't backpropagate to the readers.
        "! queue leaky=downstream max-size-buffers=4 max-size-bytes=0 max-size-time=0 "
        "! videoconvert "
        // Quality knobs:
        //  - speed-preset=faster: still real-time on a single core, but
        //    gives x264 enough lookahead to make better mode decisions
        //    than ultrafast. veryfast/faster is the usual broadcast-
        //    sweet-spot for 720p30.
        //  - bitrate: 6000 kbps at the 720p baseline, scaled linearly
        //    with canvas area (bitrateKbps above).
        //  - bframes=0: WebRTC cannot carry them. mediamtx accepts the
        //    stream, opens the peer connection, then closes it with
        //    "WebRTC doesn't support H264 streams with B-frames" and the
        //    viewer never sees a frame. HLS and SRT do not care either
        //    way, and WHEP is how the showcase plays this out, so the
        //    compression they would buy is not worth the delivery path.
        //  - profile=main: CABAC. WebRTC carries it; only B-frames are
        //    the problem, so the profile stays.
        //  - key-int-max=60: 2 s GOP at 30 fps -- matches our HLS
        //    segment cadence so I-frames align with segment boundaries.
        "! x264enc speed-preset=faster tune=zerolatency "
                  "bitrate=" + std::to_string(bitrateKbps) + " vbv-buf-capacity=2000 "
                  "bframes=0 key-int-max=60 "
        "! video/x-h264,profile=main "
        // RTSP carries codec frames over RTP, not MPEG-TS. h264parse with
        // config-interval=-1 republishes SPS/PPS on every IDR so a viewer
        // joining mid-stream can decode without waiting for the next key
        // frame.
        "! h264parse config-interval=-1 "
        // Drop-old leaky queue right before the RTSP sink: if mediamtx
        // momentarily stalls accepting RTP, throw frames away rather
        // than queue them indefinitely.
        "! queue leaky=downstream max-size-buffers=8 max-size-bytes=0 max-size-time=0 "
        "! rtspclientsink protocols=tcp location=\"" + outUrl + "\" ";

    for (std::size_t i = 0; i < flowIds.size(); ++i)
    {
        pipelineDesc +=
            "appsrc name=src" + std::to_string(i) +
            " is-live=true format=time do-timestamp=true "
            " max-bytes=20971520 block=true "
            // Per-tile leaky queue absorbs short scheduler jitter without
            // unbounded growth. 3 buffers ~ 100 ms at 30 fps.
            "! queue leaky=downstream max-size-buffers=3 max-size-bytes=0 max-size-time=0 "
            // No videoscale -- tiles composite at native source resolution.
            "! videoconvert "
            "! video/x-raw,format=I420,width=" + std::to_string(TILE_W) +
            ",height=" + std::to_string(TILE_H) + " "
            "! comp.sink_" + std::to_string(i) + " ";
    }

    g_print("Pipeline: %s\n", pipelineDesc.c_str());

    GError* parseErr = nullptr;
    auto* pipeline = ::gst_parse_launch(pipelineDesc.c_str(), &parseErr);
    if (pipeline == nullptr)
    {
        g_printerr("gst_parse_launch failed: %s\n", parseErr ? parseErr->message : "unknown");
        if (parseErr) ::g_error_free(parseErr);
        return 1;
    }

    // Open every MXL flow before going live, retrying per flow until it
    // appears (see the loop below) so a not-yet-mirrored flow doesn't wedge.
    nmos_slots::Slots slots{flowIds};
    std::vector<FlowWorker> workers(flowIds.size());
    for (std::size_t i = 0; i < flowIds.size(); ++i)
    {
        auto& w = workers[i];
        w.index = i;
        w.flowId = flowIds[i];
        w.slots = &slots;
        w.domain = domain;

        w.instance = ::mxlCreateInstance(domain.c_str(), "");
        if (w.instance == nullptr)
        {
            g_printerr("mxlCreateInstance failed for flow %s\n", w.flowId.c_str());
            return 3;
        }

        // Retry the reader open indefinitely instead of failing fast. The
        // anti-affinity keeps the compositor off the producer's node (to
        // exercise cross-node MXL), so each flow arrives via an MxlFlowMirror
        // that the operator only creates -- and its intent-GC only keeps -- while
        // a consumer pod is stably Running. Exiting on a failed open is fatal:
        // the pod restarts, the GC tears the half-formed mirror down, and it
        // never lives long enough to get a mirror -> CrashLoopBackOff, no mirror
        // ever. So never exit for a missing flow -- stay Running (container ready,
        // a stable consumer) so the mirror forms and the flow arrives. Killable
        // via SIGTERM (g_exit).
        for (int attempt = 1; !w.flowId.empty(); ++attempt)
        {
            auto ret = ::mxlCreateFlowReader(w.instance, w.flowId.c_str(), "", &w.reader);
            if (ret == MXL_STATUS_OK) break;
            if (g_exit.load(std::memory_order_relaxed)) return 0;
            if (attempt == 1 || attempt % 15 == 0)
                g_print("[%zu] %s waiting for flow (attempt %d, mxlCreateFlowReader=%d)\n",
                    i, w.flowId.c_str(), attempt, static_cast<int>(ret));
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }

        if (auto ret = w.reader ? ::mxlFlowReaderGetConfigInfo(w.reader, &w.config) : MXL_STATUS_OK;
            ret != MXL_STATUS_OK)
        {
            g_printerr("mxlFlowReaderGetConfigInfo %s failed: %d\n", w.flowId.c_str(), static_cast<int>(ret));
            return 5;
        }

        auto srcName = std::string{"src"} + std::to_string(i);
        w.appsrc = ::gst_bin_get_by_name(GST_BIN(pipeline), srcName.c_str());
        if (w.appsrc == nullptr)
        {
            g_printerr("appsrc %s not found in pipeline\n", srcName.c_str());
            return 6;
        }
    }

    if (::gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    {
        g_printerr("Failed to set pipeline PLAYING\n");
        return 7;
    }

    for (auto& w : workers)
    {
        w.thread = std::thread{worker_loop, &w};
    }

    // NMOS is on where the Node is given an address to be reached at. Off, the
    // compositor shows MXL_FLOW_IDS exactly as it always has.
    nmos_node::NodePtr node;
    std::mutex nodeMu;
    std::thread nmosStarter;
    if (auto host = env_or("NMOS_HOST_ADDRESS", ""); !host.empty())
    {
        nmos_node::Config nc;
        nc.domainPath = domain;
        nc.hostAddress = host;
        nc.httpPort = static_cast<unsigned int>(std::atoi(env_or("NMOS_HTTP_PORT", "0").c_str()));
        nc.seed = env_or("NMOS_SEED", "");
        nc.label = env_or("NMOS_LABEL", "MXL compositor");
        nc.description = env_or("NMOS_DESCRIPTION", "MXL mosaic compositor");
        nc.dnsDomain = env_or("NMOS_DNS_DOMAIN", "");
        nc.registryHost = env_or("NMOS_REGISTRY_HOST", "");
        nc.registryPort = static_cast<unsigned int>(std::atoi(env_or("NMOS_REGISTRY_PORT", "0").c_str()));
        // nmos-cpp serves the IS-09 System API beside its registry, so a
        // registry named by address is the System API too unless told apart.
        nc.systemHost = env_or("NMOS_SYSTEM_HOST", nc.registryHost.c_str());
        nc.systemPort = static_cast<unsigned int>(std::atoi(
            env_or("NMOS_SYSTEM_PORT", env_or("NMOS_REGISTRY_PORT", "0").c_str()).c_str()));
        nc.frameWidth = g_frameW;
        nc.frameHeight = g_frameH;
        if (nc.seed.empty())
        {
            // Resource ids are derived from the seed, so without one they
            // change on every restart and a controller's connections point at
            // resources that no longer exist.
            g_printerr("NMOS_SEED is required with NMOS_HOST_ADDRESS\n");
            return 8;
        }
        // Started once the domain has its identity, and not before: an MXL
        // Receiver with no domain to name is one no controller can route to,
        // so none is published. The platform may write domain_def.json after
        // this starts, which is why this retries rather than exits; the
        // mosaic runs from MXL_FLOW_IDS meanwhile.
        nmosStarter = std::thread{[nc, &slots, &node, &nodeMu]() {
            for (int attempt = 0; !g_exit.load(std::memory_order_relaxed); ++attempt)
            {
                std::string err;
                auto started = nmos_node::start(nc, slots, err);
                if (started)
                {
                    std::lock_guard<std::mutex> lock{nodeMu};
                    node = std::move(started);
                    return;
                }
                if (attempt % 6 == 0) g_printerr("NMOS not started: %s\n", err.c_str());
                for (int i = 0; i < 20 && !g_exit.load(std::memory_order_relaxed); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }};
    }

    // Diagnostic ticker: every 5s, report per-flow push/miss counters.
    // Without this it's impossible to tell whether the pipeline is silent
    // because workers aren't getting grains, or because grains are being
    // pushed but dropped downstream.
    constexpr int kStatsIntervalS = 5;
    std::thread stats{[&workers, kStatsIntervalS]() {
        std::uint64_t last[16] = {0};
        while (!g_exit.load(std::memory_order_relaxed))
        {
            std::this_thread::sleep_for(std::chrono::seconds(kStatsIntervalS));
            std::string line = "STATS";
            for (std::size_t i = 0; i < workers.size(); ++i)
            {
                auto cur = workers[i].framesPushed.load();
                auto delta = cur - last[i];
                last[i] = cur;
                workers[i].fps.store(static_cast<double>(delta) / kStatsIntervalS,
                    std::memory_order_relaxed);
                line += " [" + std::to_string(i) + "]=" + std::to_string(delta) + "fps";
            }
            g_print("%s\n", line.c_str());
        }
    }};

    // Per-flow delivery metrics endpoint.
    int statsPort = std::atoi(env_or("MXL_STATS_PORT", "9090").c_str());
    if (statsPort <= 0) statsPort = 9090;
    std::thread statsHttp{stats_http_server, &workers, statsPort};

    // Block on bus until ERROR / EOS / SIGTERM.
    auto* bus = ::gst_element_get_bus(pipeline);
    while (!g_exit.load(std::memory_order_relaxed))
    {
        auto* msg = ::gst_bus_timed_pop_filtered(bus, 500 * GST_MSECOND,
            static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_WARNING));
        if (msg == nullptr) continue;
        switch (GST_MESSAGE_TYPE(msg))
        {
            case GST_MESSAGE_ERROR:
            {
                GError* err = nullptr;
                gchar* dbg = nullptr;
                ::gst_message_parse_error(msg, &err, &dbg);
                g_printerr("BUS ERROR %s: %s\n", err ? err->message : "?", dbg ? dbg : "");
                if (err) ::g_error_free(err);
                ::g_free(dbg);
                g_exit.store(true);
                break;
            }
            case GST_MESSAGE_EOS:
                g_print("BUS EOS\n");
                g_exit.store(true);
                break;
            case GST_MESSAGE_WARNING:
            {
                GError* err = nullptr;
                gchar* dbg = nullptr;
                ::gst_message_parse_warning(msg, &err, &dbg);
                g_print("BUS WARN %s: %s\n", err ? err->message : "?", dbg ? dbg : "");
                if (err) ::g_error_free(err);
                ::g_free(dbg);
                break;
            }
            default: break;
        }
        ::gst_message_unref(msg);
    }
    ::gst_object_unref(bus);

    if (nmosStarter.joinable()) nmosStarter.join();
    {
        std::lock_guard<std::mutex> lock{nodeMu};
        node.reset();
    }
    g_print("Shutting down workers...\n");
    if (stats.joinable()) stats.join();
    if (statsHttp.joinable()) statsHttp.join();
    for (auto& w : workers)
    {
        if (w.thread.joinable()) w.thread.join();
    }

    ::gst_element_set_state(pipeline, GST_STATE_NULL);
    ::gst_object_unref(pipeline);

    for (auto& w : workers)
    {
        if (w.appsrc) ::gst_object_unref(w.appsrc);
        if (w.reader) ::mxlReleaseFlowReader(w.instance, w.reader);
        if (w.instance) ::mxlDestroyInstance(w.instance);
    }

    return 0;
}
