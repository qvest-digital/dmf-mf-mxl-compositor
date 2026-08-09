// mxl-audio-preview
//
// Serves audible previews of MXL AUDIO flows, which mediamtx cannot do:
// its mxlSource refuses anything but video ("flow <id> is not video
// (format=audio)"), so there is no path from an audio/float32 flow to a
// browser. This reads the flow's per-channel ring buffers via libmxl,
// interleaves them into F32LE, encodes Opus and RTSP-publishes to mediamtx,
// which then serves it like any other path (WebRTC/WHEP, HLS fallback).
//
// Two channels are published, whatever the flow is wide. Nothing downstream
// carries more: opusenc tops out at eight channels, RTP Opus at two, and the
// MPEG-TS HLS variant delivers no more than stereo either -- a wider flow fails
// to negotiate outright rather than degrading. Which two is the caller's
// choice, so every channel of a 12-channel flow is reachable a pair at a time,
// and /status carries a level for each of them so the silent ones are visible
// without listening to every pair in turn.
//
// On demand rather than always-on: a caller opens one flow at a time out of an
// inventory that can hold any number of audio flows, so a process per flow
// would not scale. The control API takes POST /start?flow=<uuid> and
// DELETE /stop?flow=<uuid>, and serves up to MXL_MAX_SESSIONS at once.
//
//   POST   /start?flow=<uuid>[&channels=<l>[,<r>]]
//                             -> {"path":"preview-audio-<uuid>", ...}
//   DELETE /stop?flow=<uuid>   -> {"stopped":"<uuid>"}
//   GET    /status             -> sessions, samples pushed, per-channel peak
//
// One reader, two published paths: preview-audio-<uuid> in Opus for WHEP and
// preview-audio-<uuid>-hls in AAC for the HLS fallback. See pipeline_desc for
// why both are needed. Both paths must exist on the RTSP server before /start.
//
// The peak levels in /status are a byte-for-byte read of what was pushed, so
// "is this flow actually carrying audio" is answerable without a browser. The
// UI's bars and spectrum are drawn client-side off the decoded stream instead --
// no reason to push meter data when the audio itself is already there.
//
// Env: MXL_DOMAIN (default /run/mxl/domain), MXL_PREVIEW_RTSP_BASE
// (default rtsp://mediamtx:8554/preview-audio-), MXL_CONTROL_PORT (8090),
// MXL_MAX_SESSIONS (4), MXL_OPUS_BITRATE (128000), MXL_AAC_BITRATE (128000).

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <gst/app/gstappsrc.h>
#include <gst/gst.h>

#include <mxl/dataformat.h>
#include <mxl/flow.h>
#include <mxl/mxl.h>
#include <mxl/time.h>

namespace
{
    std::atomic<bool> g_exit{false};
    void on_signal(int) { g_exit.store(true, std::memory_order_relaxed); }

    std::string env_or(char const* key, char const* fallback)
    {
        char const* v = std::getenv(key);
        return v ? std::string{v} : std::string{fallback};
    }

    int env_int(char const* key, int fallback)
    {
        char const* v = std::getenv(key);
        if (v == nullptr) return fallback;
        try
        {
            return std::stoi(v);
        }
        catch (...)
        {
            return fallback;
        }
    }

    // Canonical 8-4-4-4-12. The flow id lands in an RTSP path and an MXL domain
    // path, so it is validated here and not merely trusted from the caller.
    bool valid_uuid(std::string const& s)
    {
        if (s.size() != 36) return false;
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            bool const dash = (i == 8 || i == 13 || i == 18 || i == 23);
            if (dash != (s[i] == '-')) return false;
            if (!dash && std::isxdigit(static_cast<unsigned char>(s[i])) == 0) return false;
        }
        return true;
    }

    // How many samples behind the write head to stay. The producer commits in
    // batches of maxCommitBatchSizeHint, so anything newer than one batch may be
    // half-written -- the same hazard the compositor hit on video grains, where
    // reading the head produced torn frames.
    constexpr int kLagBatches = 2;

    // Above this, the payload cannot be normalised float PCM: +12 dBFS is
    // already impossible for float samples meant to sit in [-1,1], and real
    // audio never sustains it.
    constexpr float kMaxPlausibleSample = 4.0F;

    // Meter fallback rate. A chunk's peak on its own is a ~20ms sample of the
    // audio, and /status is read far less often than a chunk is read, so
    // reporting it raw hands a caller one short window out of every poll
    // interval and hides everything between: quiet where the audio was loud,
    // and a value that jumps rather than moves. Decaying the reported level
    // towards each new peak instead makes it an envelope of every sample read,
    // which is what a level meter shows. Fast attack, this much fall.
    constexpr double kLevelDecayDbPerSecond = 30.0;

    // Bounded, unlike the compositor's open-ended wait for a flow to appear:
    // that one blocks its pipeline forever when a flow never comes back, and a
    // preview must fail visibly instead of hanging the overlay.
    constexpr int kOpenAttempts = 15;
    constexpr auto kOpenInterval = std::chrono::seconds{1};

    // What gets published, independent of how wide the flow is. See the note at
    // the top for why nothing downstream carries more.
    constexpr std::uint32_t kOutChannels = 2;

    struct Session
    {
        std::string flowId;
        std::string path;
        std::thread worker;
        std::atomic<bool> stop{false};

        // Which two source channels are published, zero-based. Written by
        // /start and read by the worker on every chunk, so a caller can move to
        // another pair without the pipeline being torn down: the caps stay at
        // kOutChannels, only which samples are copied into them changes. The
        // worker clamps them to the flow's width once it knows it, and stores
        // the clamped value back so /status reports what is actually audible.
        std::atomic<std::uint32_t> left{0};
        std::atomic<std::uint32_t> right{1};

        // Written by the worker, read by /status. Coarse enough that relaxed
        // atomics are the right amount of synchronisation.
        std::atomic<bool> running{false};
        std::atomic<std::uint64_t> samples{0};
        std::atomic<std::uint32_t> channels{0};
        std::atomic<std::uint32_t> rate{0};
        std::atomic<int> peakMilliDb{-12000};  // -120.00 dBFS == silence
        // Latched when the payload turns out not to be normalised float PCM;
        // the reader keeps going but pushes silence. See the check in the loop.
        std::atomic<bool> muted{false};
        std::mutex errMu;
        std::string error;
        // One level per source channel, not just the published pair: the point
        // of a wide flow is finding which channels carry anything. Sized by the
        // worker once the flow is open, so a mutex rather than atomics.
        std::mutex levelMu;
        std::vector<int> chanPeakMilliDb;

        void setError(std::string e)
        {
            std::lock_guard<std::mutex> lk{errMu};
            error = std::move(e);
        }

        std::string getError()
        {
            std::lock_guard<std::mutex> lk{errMu};
            return error;
        }

        void setLevels(std::vector<float> const& peaks)
        {
            std::lock_guard<std::mutex> lk{levelMu};
            chanPeakMilliDb.resize(peaks.size());
            for (std::size_t c = 0; c < peaks.size(); ++c)
            {
                chanPeakMilliDb[c] = peaks[c] > 0.0F
                    ? std::max(static_cast<int>(std::lround(2000.0 * std::log10(peaks[c]))), -12000)
                    : -12000;
            }
        }

        std::vector<int> getLevels()
        {
            std::lock_guard<std::mutex> lk{levelMu};
            return chanPeakMilliDb;
        }
    };

    std::mutex g_mu;
    std::map<std::string, std::unique_ptr<Session>> g_sessions;
    std::string g_domain;
    std::string g_rtspBase;
    int g_maxSessions = 4;
    int g_opusBitrate = 128000;
    int g_aacBitrate = 128000;

    // The HLS-side path is the WHEP path plus this suffix. A caller has to
    // create both paths before calling /start, so it derives the names by the
    // same rule; the two have to stay in step.
    constexpr char const* kHlsPathSuffix = "-hls";

    // Planar -> interleaved stereo. Each channel owns its own ring buffer;
    // stride is the byte distance between the same sample position in
    // consecutive channels, and the two fragments cover a read that straddles
    // the buffer's wraparound point.
    //
    // Every channel is read even though two are published, because `peaks`
    // covers the whole flow -- the pass is over the samples either way, and a
    // level for a channel nobody selected is the only way to tell a silent
    // channel from an unheard one. `out` carries the selected pair alone, in
    // left-then-right order regardless of which is the lower index.
    void select_pair(mxlWrappedMultiBufferSlice const& slices, std::uint32_t channels,
        std::uint32_t left, std::uint32_t right, std::vector<float>& out,
        std::vector<float>& peaks)
    {
        out.clear();
        peaks.assign(channels, 0.0F);
        for (auto const& fr : slices.base.fragments)
        {
            if (fr.pointer == nullptr || fr.size == 0) continue;
            auto const* bytes = static_cast<std::uint8_t const*>(fr.pointer);
            std::size_t const n = fr.size / sizeof(float);
            for (std::size_t s = 0; s < n; ++s)
            {
                float l = 0.0F;
                float r = 0.0F;
                for (std::uint32_t c = 0; c < channels; ++c)
                {
                    float v = 0.0F;
                    // memcpy rather than a float* cast: the ring buffer carries
                    // no alignment guarantee for an arbitrary sample offset.
                    std::memcpy(&v, bytes + (c * slices.stride) + (s * sizeof(float)), sizeof(float));
                    peaks[c] = std::max(peaks[c], std::fabs(v));
                    if (c == left) l = v;
                    if (c == right) r = v;
                }
                out.push_back(l);
                out.push_back(r);
            }
        }
    }

    // Two codecs from one reader, because the two ways a browser can play this
    // back disagree about audio:
    //
    //   WHEP/WebRTC     carries Opus and cannot carry AAC at all.
    //   HLS (mpegts)    carries AAC only -- mediamtx refuses an Opus track with
    //                   "the MPEG-TS variant of HLS supports MPEG-4 Audio only"
    //                   and its muxer dies the moment the path is published.
    //
    // The overlay tries WHEP first and falls back to HLS, so a single-codec
    // publish leaves one of those two paths broken. Publishing both costs one
    // extra encoder per session and keeps the fallback working on clusters where
    // ICE never completes.
    //
    // rtspclientsink picks an AAC payloader by rank, and GStreamer ships two at
    // the same rank: rtpmp4apay (MP4A-LATM) and rtpmp4gpay (mpeg4-generic).
    // LATM wins by default, and mediamtx then accepts the track but its MPEG-TS
    // HLS muxer dies on it -- "unsupported frameLengthType 2" -- so the AAC path
    // publishes and still serves nothing, which is the failure this whole branch
    // exists to avoid. The payloader cannot be chosen from a parse-launch string
    // (rtspclientsink exposes it only as a pad property of GstElement type, and
    // a pre-payloaded RTP stream will not link), so demote LATM in the registry
    // instead. Process-local: this binary builds no other pipeline.
    void prefer_mpeg4_generic_aac()
    {
        if (auto* feature = ::gst_registry_lookup_feature(::gst_registry_get(), "rtpmp4apay"))
        {
            ::gst_plugin_feature_set_rank(feature, GST_RANK_NONE);
            ::gst_object_unref(feature);
        }
    }

    std::string pipeline_desc(std::string const& opusLocation, std::string const& aacLocation,
        std::uint32_t rate)
    {
        std::ostringstream os;
        // is-live + do-timestamp: the reader is paced by mxlSleepUntil against
        // the flow's own sample clock, so GStreamer timestamps arrivals rather
        // than us computing PTS from a sample counter that would drift off it.
        //
        // The channel count is the published one, not the flow's: the reader
        // hands over a pair whatever it opened, so the caps never depend on how
        // wide the flow is. Feeding the flow's own width in was what made a
        // 12-channel flow fail with not-negotiated -- above eight channels
        // there is no encoder to link to, and above two no channel mask either.
        os << "appsrc name=src is-live=true format=time do-timestamp=true"
              " caps=audio/x-raw,format=F32LE,layout=interleaved,rate="
           << rate << ",channels=" << kOutChannels
           << " ! queue leaky=downstream max-size-buffers=32 max-size-bytes=0 max-size-time=0"
              " ! audioconvert ! audioresample"
        // Opus needs 48k; a 48k flow passes through audioresample untouched. AAC
        // is happy at 48k too, so both branches share the one resample.
           << " ! audio/x-raw,rate=48000 ! tee name=enc"
        // Each tee branch needs its own queue: without them a stall in one
        // encoder blocks the other, and with a live source that deadlocks both.
        // The per-branch audioconvert is not redundant with the one above -- the
        // two encoders want different layouts (avenc_aac takes planar float,
        // opusenc interleaved), and it is a no-op when the format already fits.
           << " enc. ! queue leaky=downstream max-size-buffers=32 max-size-bytes=0 max-size-time=0"
              " ! audioconvert ! opusenc bitrate=" << g_opusBitrate
           << " ! rtspclientsink protocols=tcp location=\"" << opusLocation << "\""
           << " enc. ! queue leaky=downstream max-size-buffers=32 max-size-bytes=0 max-size-time=0"
              " ! audioconvert ! avenc_aac bitrate=" << g_aacBitrate
           << " ! aacparse"
           << " ! rtspclientsink protocols=tcp location=\"" << aacLocation << "\"";
        return os.str();
    }

    void run_session(Session* ses)
    {
        ::mxlInstance instance = ::mxlCreateInstance(g_domain.c_str(), "");
        if (instance == nullptr)
        {
            ses->setError("mxlCreateInstance failed for domain " + g_domain);
            g_printerr("[%s] mxlCreateInstance failed\n", ses->flowId.c_str());
            return;
        }

        ::mxlFlowReader reader = nullptr;
        mxlStatus openRet = MXL_STATUS_OK;
        for (int attempt = 1; attempt <= kOpenAttempts; ++attempt)
        {
            if (ses->stop.load(std::memory_order_relaxed)) break;
            openRet = ::mxlCreateFlowReader(instance, ses->flowId.c_str(), "", &reader);
            if (openRet == MXL_STATUS_OK) break;
            reader = nullptr;
            std::this_thread::sleep_for(kOpenInterval);
        }
        if (reader == nullptr)
        {
            std::ostringstream os;
            os << "flow not readable on this node (mxlCreateFlowReader=" << static_cast<int>(openRet)
               << " after " << kOpenAttempts << " attempts)";
            ses->setError(os.str());
            g_printerr("[%s] %s\n", ses->flowId.c_str(), os.str().c_str());
            ::mxlDestroyInstance(instance);
            return;
        }

        ::mxlFlowConfigInfo config{};
        if (::mxlFlowReaderGetConfigInfo(reader, &config) != MXL_STATUS_OK)
        {
            ses->setError("mxlFlowReaderGetConfigInfo failed");
            ::mxlReleaseFlowReader(instance, reader);
            ::mxlDestroyInstance(instance);
            return;
        }
        if (config.common.format != MXL_DATA_FORMAT_AUDIO)
        {
            ses->setError("flow is not an audio flow");
            g_printerr("[%s] format=%u is not audio\n", ses->flowId.c_str(), config.common.format);
            ::mxlReleaseFlowReader(instance, reader);
            ::mxlDestroyInstance(instance);
            return;
        }

        auto const channels = config.continuous.channelCount;
        auto const rate = config.common.grainRate;  // sample rate for AUDIO
        if (channels == 0 || rate.numerator == 0 || rate.denominator == 0)
        {
            ses->setError("flow reports no channels or no sample rate");
            ::mxlReleaseFlowReader(instance, reader);
            ::mxlDestroyInstance(instance);
            return;
        }
        auto const rateHz = static_cast<std::uint32_t>(rate.numerator / rate.denominator);

        std::size_t maxRead = 0;
        ::mxlFlowReaderGetMaxReadLengthSamples(reader, &maxRead);
        auto const batch = std::max<std::uint32_t>(config.common.maxCommitBatchSizeHint, 1);
        // One read per ~20ms of audio keeps Opus fed at its own frame size
        // without a read per sample, clamped to what the buffer allows.
        std::uint32_t chunk = std::max<std::uint32_t>(rateHz / 50, batch);
        if (maxRead > 0) chunk = std::min<std::uint32_t>(chunk, static_cast<std::uint32_t>(maxRead));
        chunk = std::max<std::uint32_t>(chunk, 1);

        ses->channels.store(channels, std::memory_order_relaxed);
        ses->rate.store(rateHz, std::memory_order_relaxed);
        g_print("[%s] audio flow: %u ch @ %uHz, batch=%u chunk=%u -> %s\n",
            ses->flowId.c_str(), channels, rateHz, batch, chunk, ses->path.c_str());

        auto const opusLocation = g_rtspBase + ses->flowId;
        auto const desc = pipeline_desc(opusLocation, opusLocation + kHlsPathSuffix, rateHz);
        GError* err = nullptr;
        GstElement* pipeline = ::gst_parse_launch(desc.c_str(), &err);
        if (pipeline == nullptr || err != nullptr)
        {
            std::string msg = err ? err->message : "gst_parse_launch failed";
            if (err) ::g_error_free(err);
            ses->setError("pipeline: " + msg);
            g_printerr("[%s] pipeline: %s\n", ses->flowId.c_str(), msg.c_str());
            ::mxlReleaseFlowReader(instance, reader);
            ::mxlDestroyInstance(instance);
            return;
        }
        auto* appsrc = GST_APP_SRC(::gst_bin_get_by_name(GST_BIN(pipeline), "src"));
        ::gst_element_set_state(pipeline, GST_STATE_PLAYING);
        ses->running.store(true, std::memory_order_relaxed);

        // Paced by the writer's commit head, not by wall clock. The two are not
        // interchangeable: a producer that drifts or stalls leaves its head
        // behind real time, and a clock-paced reader then asks for samples that
        // are inside the ring but not yet committed -- which returns data (the
        // ring's uninitialised bytes, reinterpreted as absurd floats) rather
        // than an error. Waiting for headIndex to pass what we want is the only
        // thing that guarantees committed samples. Matches how the SDK's own
        // gst mxlsrc drives its audio reads.
        std::uint64_t const lag = static_cast<std::uint64_t>(kLagBatches) * batch;
        std::uint64_t const ring = config.continuous.bufferLength;
        // A quarter chunk: short enough to keep latency near one chunk, long
        // enough not to spin on the runtime info.
        auto const pollNs = static_cast<std::uint64_t>(
            250'000'000ULL * chunk / std::max<std::uint32_t>(rateHz, 1));

        ::mxlFlowRuntimeInfo rt{};
        if (::mxlFlowReaderGetRuntimeInfo(reader, &rt) != MXL_STATUS_OK)
        {
            ses->setError("mxlFlowReaderGetRuntimeInfo failed");
            ::gst_element_set_state(pipeline, GST_STATE_NULL);
            ::gst_object_unref(pipeline);
            ::mxlReleaseFlowReader(instance, reader);
            ::mxlDestroyInstance(instance);
            return;
        }
        std::uint64_t index = rt.headIndex > (chunk + lag) ? rt.headIndex - chunk - lag : 0;

        std::vector<float> pcm;
        pcm.reserve(static_cast<std::size_t>(chunk) * kOutChannels);
        std::vector<float> peaks;
        // Per-channel meter envelope, and how much of it one chunk's worth of
        // silence takes away. Held here rather than on the session because only
        // this thread advances it; /status reads the published copy.
        std::vector<float> envelope;
        auto const chunkSeconds = static_cast<double>(chunk) / std::max<std::uint32_t>(rateHz, 1);
        auto const chunkDecay =
            static_cast<float>(std::pow(10.0, -kLevelDecayDbPerSecond * chunkSeconds / 20.0));
        int emptyReads = 0;
        int implausible = 0;
        bool logged = false;

        // Nothing watched the bus before, and that is why a broken encoder branch
        // was invisible: the Opus sink kept publishing, /status kept saying
        // running, and the AAC path silently never appeared. A pipeline error is
        // latched into the session so /api/preview reports it and the overlay can
        // say what went wrong instead of spinning.
        GstBus* bus = ::gst_element_get_bus(pipeline);

        while (!ses->stop.load(std::memory_order_relaxed) && !g_exit.load(std::memory_order_relaxed))
        {
            if (GstMessage* msg = ::gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR))
            {
                GError* gerr = nullptr;
                gchar* dbg = nullptr;
                ::gst_message_parse_error(msg, &gerr, &dbg);
                std::string const from = GST_OBJECT_NAME(GST_MESSAGE_SRC(msg));
                std::string const what = gerr ? gerr->message : "unknown pipeline error";
                // First one wins: later errors are usually the teardown cascade.
                if (ses->getError().empty()) ses->setError(from + ": " + what);
                g_printerr("[%s] pipeline error from %s: %s (%s)\n", ses->flowId.c_str(),
                    from.c_str(), what.c_str(), dbg ? dbg : "no detail");
                if (gerr) ::g_error_free(gerr);
                ::g_free(dbg);
                ::gst_message_unref(msg);
            }
            if (::mxlFlowReaderGetRuntimeInfo(reader, &rt) != MXL_STATUS_OK)
            {
                ::mxlSleepUntil(::mxlGetTime() + pollNs);
                continue;
            }
            // Not committed yet -- wait, do not read it. This is the check that
            // keeps uninitialised ring bytes out of the stream.
            if (rt.headIndex < index + chunk + lag)
            {
                ::mxlSleepUntil(::mxlGetTime() + pollNs);
                continue;
            }
            // Fallen so far behind that the writer is about to lap us: skip to
            // the live edge rather than reading samples about to be overwritten.
            if (ring > 0 && rt.headIndex - index > ring - 2 * chunk)
            {
                index = rt.headIndex - chunk - lag;
                g_print("[%s] reader lapped; skipped to the live edge\n", ses->flowId.c_str());
            }

            mxlWrappedMultiBufferSlice slices{};
            auto const ret = ::mxlFlowReaderGetSamples(reader, index, chunk, 100'000'000ULL, &slices);
            if (ret != MXL_STATUS_OK)
            {
                // Outside the ring (producer restarted, or a long stall):
                // nudging sample by sample cannot recover, so drop onto the
                // live edge.
                if (++emptyReads % 50 == 1)
                    g_printerr("[%s] mxlFlowReaderGetSamples=%d at index %llu (head %llu); realigning\n",
                        ses->flowId.c_str(), static_cast<int>(ret),
                        static_cast<unsigned long long>(index),
                        static_cast<unsigned long long>(rt.headIndex));
                index = rt.headIndex > (chunk + lag) ? rt.headIndex - chunk - lag : 0;
                continue;
            }
            emptyReads = 0;

            if (!logged)
            {
                logged = true;
                g_print("[%s] slice: stride=%zu count=%zu frag0=%zu frag1=%zu\n",
                    ses->flowId.c_str(), slices.stride, slices.count,
                    slices.base.fragments[0].size, slices.base.fragments[1].size);
            }

            // Re-read per chunk: /start can move the pair while this runs, and
            // the caps do not change, so the switch costs one chunk of audio
            // rather than a pipeline rebuild. Clamped here because /start
            // answers before the flow is open and cannot know its width; the
            // clamped value is stored back so /status reports what is audible.
            auto const lastCh = channels - 1;
            std::uint32_t left = ses->left.load(std::memory_order_relaxed);
            std::uint32_t right = ses->right.load(std::memory_order_relaxed);
            if (left > lastCh)
            {
                left = lastCh;
                ses->left.store(left, std::memory_order_relaxed);
            }
            if (right > lastCh)
            {
                right = lastCh;
                ses->right.store(right, std::memory_order_relaxed);
            }

            select_pair(slices, channels, left, right, pcm, peaks);
            float peak = 0.0F;
            for (std::size_t i = 0; i < pcm.size(); ++i) peak = std::max(peak, std::fabs(pcm[i]));

            // A flow can declare audio/float32 and still not carry normalised
            // PCM. Transport-integrity producers write an incrementing byte
            // ramp (80 81 82 83 ...); read as float32 that reaches ~1e38, and
            // publishing it would be full-scale noise in someone's headphones.
            // So say what was found and send silence rather than the noise --
            // /status carries the verdict, and the reader keeps running so the
            // meters and timing stay honest instead of the overlay just hanging.
            // Levels for every channel, before any muting: what the flow
            // carries is the diagnostic, and it stays true whether or not the
            // pair on its way out has been replaced with silence.
            envelope.resize(peaks.size(), 0.0F);
            for (std::size_t c = 0; c < peaks.size(); ++c)
            {
                envelope[c] = std::max(peaks[c], envelope[c] * chunkDecay);
            }
            ses->setLevels(envelope);
            float const flowPeak = peaks.empty() ? 0.0F : *std::max_element(peaks.begin(), peaks.end());

            // Judged on the whole flow rather than the audible pair: the
            // verdict is about the payload, and a ramp on a channel nobody
            // selected is the same broken producer.
            if (!ses->muted.load(std::memory_order_relaxed))
            {
                // Latch only after several consecutive chunks: one implausible
                // read should not mute a genuinely loud flow.
                if (flowPeak > kMaxPlausibleSample)
                {
                    if (++implausible == 5)
                    {
                        ses->muted.store(true, std::memory_order_relaxed);
                        ses->setError("flow does not carry normalised float PCM "
                                      "(peak far above full scale) -- muted; "
                                      "looks like a synthetic test pattern");
                        g_printerr("[%s] payload is not normalised float PCM "
                                   "(peak %g); muting output\n",
                            ses->flowId.c_str(), static_cast<double>(flowPeak));
                    }
                }
                else
                {
                    implausible = 0;
                }
            }
            if (ses->muted.load(std::memory_order_relaxed) && !pcm.empty())
            {
                std::fill(pcm.begin(), pcm.end(), 0.0F);
                peak = 0.0F;
            }

            if (!pcm.empty())
            {
                auto const bytes = pcm.size() * sizeof(float);
                GstBuffer* buf = ::gst_buffer_new_allocate(nullptr, bytes, nullptr);
                GstMapInfo map{};
                if (::gst_buffer_map(buf, &map, GST_MAP_WRITE) == TRUE)
                {
                    std::memcpy(map.data, pcm.data(), bytes);
                    ::gst_buffer_unmap(buf, &map);
                    if (::gst_app_src_push_buffer(appsrc, buf) != GST_FLOW_OK)
                    {
                        ses->setError("downstream refused a buffer (publish failed?)");
                        ::gst_buffer_unref(buf);
                        break;
                    }
                }
                else
                {
                    ::gst_buffer_unref(buf);
                }
                ses->samples.fetch_add(pcm.size() / kOutChannels, std::memory_order_relaxed);
                // 20*log10(peak), floored at -120 dBFS so silence is a number
                // rather than -inf.
                int const milliDb = peak > 0.0F
                    ? static_cast<int>(std::lround(2000.0 * std::log10(peak)))
                    : -12000;
                ses->peakMilliDb.store(std::max(milliDb, -12000), std::memory_order_relaxed);
            }

            index += chunk;
        }

        g_print("[%s] stopping (%llu samples pushed)\n", ses->flowId.c_str(),
            static_cast<unsigned long long>(ses->samples.load(std::memory_order_relaxed)));
        ses->running.store(false, std::memory_order_relaxed);
        if (appsrc != nullptr)
        {
            ::gst_app_src_end_of_stream(appsrc);
            ::gst_object_unref(appsrc);
        }
        ::gst_object_unref(bus);
        ::gst_element_set_state(pipeline, GST_STATE_NULL);
        ::gst_object_unref(pipeline);
        ::mxlReleaseFlowReader(instance, reader);
        ::mxlDestroyInstance(instance);
    }

    // -- Control server ------------------------------------------------------

    std::string json_escape(std::string const& s)
    {
        std::string out;
        for (char c : s)
        {
            if (c == '"' || c == '\\') out += '\\';
            if (static_cast<unsigned char>(c) < 0x20) continue;
            out += c;
        }
        return out;
    }

    std::string query_param(std::string const& target, char const* key)
    {
        auto const q = target.find('?');
        if (q == std::string::npos) return {};
        std::string const needle = std::string{key} + "=";
        auto pos = target.find(needle, q);
        if (pos == std::string::npos) return {};
        pos += needle.size();
        auto end = target.find_first_of("&", pos);
        return target.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }

    // "channels=<l>[,<r>]", one-based, to the zero-based pair the worker reads.
    // A single channel is published to both sides rather than rejected: soloing
    // one channel of a wide flow is the same request with nothing to pair it
    // with. Anything unparseable falls back to the first pair, which is what a
    // caller that does not care about channels wants.
    std::pair<std::uint32_t, std::uint32_t> parse_pair(std::string const& spec)
    {
        auto one_based = [](std::string const& s, std::uint32_t fallback) {
            try
            {
                auto const n = std::stoul(s);
                return n >= 1 ? static_cast<std::uint32_t>(n - 1) : fallback;
            }
            catch (...)
            {
                return fallback;
            }
        };
        if (spec.empty()) return {0, 1};
        auto const comma = spec.find(',');
        if (comma == std::string::npos)
        {
            auto const only = one_based(spec, 0);
            return {only, only};
        }
        auto const left = one_based(spec.substr(0, comma), 0);
        return {left, one_based(spec.substr(comma + 1), left)};
    }

    std::string start_session(std::string const& flow, std::string const& channels, int& code)
    {
        if (!valid_uuid(flow))
        {
            code = 400;
            return R"({"error":"bad flow id"})";
        }
        auto const [left, right] = parse_pair(channels);
        std::lock_guard<std::mutex> lk{g_mu};
        auto it = g_sessions.find(flow);
        if (it != g_sessions.end())
        {
            // Idempotent: a repeated /start must not tear down a session that
            // is already publishing. It does move the pair, though -- the caps
            // are the same two channels either way, so switching costs a chunk
            // of audio rather than a reconnect, and the browser keeps playing
            // the path it is already on.
            it->second->left.store(left, std::memory_order_relaxed);
            it->second->right.store(right, std::memory_order_relaxed);
            auto const err = it->second->getError();
            code = err.empty() ? 200 : 500;
            std::ostringstream os;
            os << R"({"path":"preview-audio-)" << flow << R"(","reused":true)";
            if (!err.empty()) os << R"(,"error":")" << json_escape(err) << '"';
            os << '}';
            return os.str();
        }
        if (g_sessions.size() >= static_cast<std::size_t>(g_maxSessions))
        {
            code = 429;
            return R"({"error":"too many concurrent audio previews"})";
        }

        auto ses = std::make_unique<Session>();
        ses->flowId = flow;
        ses->path = "preview-audio-" + flow;
        ses->left.store(left, std::memory_order_relaxed);
        ses->right.store(right, std::memory_order_relaxed);
        auto* raw = ses.get();
        ses->worker = std::thread{[raw] { run_session(raw); }};
        g_sessions.emplace(flow, std::move(ses));
        code = 200;
        std::ostringstream os;
        os << R"({"path":"preview-audio-)" << flow << R"(","started":true})";
        return os.str();
    }

    std::string stop_session(std::string const& flow, int& code)
    {
        std::unique_ptr<Session> ses;
        {
            std::lock_guard<std::mutex> lk{g_mu};
            auto it = g_sessions.find(flow);
            if (it == g_sessions.end())
            {
                code = 404;
                return R"({"error":"no such session"})";
            }
            ses = std::move(it->second);
            g_sessions.erase(it);
        }
        // Joined outside the lock: the worker can be mid-read with a 100ms
        // timeout, and holding g_mu that long would stall /status and any
        // concurrent start.
        ses->stop.store(true, std::memory_order_relaxed);
        if (ses->worker.joinable()) ses->worker.join();
        code = 200;
        return std::string{R"({"stopped":")"} + flow + R"("})";
    }

    std::string status_json()
    {
        std::ostringstream os;
        os << R"({"sessions":[)";
        std::lock_guard<std::mutex> lk{g_mu};
        bool first = true;
        for (auto const& [flow, ses] : g_sessions)
        {
            if (!first) os << ',';
            first = false;
            auto const err = ses->getError();
            os << R"({"flow":")" << flow << R"(","path":")" << ses->path << '"'
               << R"(,"running":)" << (ses->running.load(std::memory_order_relaxed) ? "true" : "false")
               << R"(,"channels":)" << ses->channels.load(std::memory_order_relaxed)
               << R"(,"rate":)" << ses->rate.load(std::memory_order_relaxed)
               << R"(,"samples":)" << ses->samples.load(std::memory_order_relaxed)
               << R"(,"peakDb":)" << (ses->peakMilliDb.load(std::memory_order_relaxed) / 100.0)
               << R"(,"muted":)" << (ses->muted.load(std::memory_order_relaxed) ? "true" : "false")
            // One-based, matching what /start takes, and post-clamp, so this is
            // the pair actually going out rather than the one that was asked
            // for.
               << R"(,"selected":[)" << (ses->left.load(std::memory_order_relaxed) + 1) << ','
               << (ses->right.load(std::memory_order_relaxed) + 1) << ']';
            os << R"(,"channelPeakDb":[)";
            bool firstLevel = true;
            for (int const milliDb : ses->getLevels())
            {
                if (!firstLevel) os << ',';
                firstLevel = false;
                os << (milliDb / 100.0);
            }
            os << ']';
            if (!err.empty()) os << R"(,"error":")" << json_escape(err) << '"';
            os << '}';
        }
        os << R"(],"maxSessions":)" << g_maxSessions << '}';
        return os.str();
    }

    void serve(int port)
    {
        int srv = ::socket(AF_INET, SOCK_STREAM, 0);
        if (srv < 0)
        {
            g_printerr("control: socket() failed\n");
            return;
        }
        int one = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(static_cast<std::uint16_t>(port));
        if (::bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        {
            g_printerr("control: bind(:%d) failed\n", port);
            ::close(srv);
            return;
        }
        if (::listen(srv, 16) < 0)
        {
            g_printerr("control: listen() failed\n");
            ::close(srv);
            return;
        }
        timeval tv{};
        tv.tv_sec = 1;
        ::setsockopt(srv, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        g_print("control: listening on :%d (POST /start?flow=, DELETE /stop?flow=, GET /status)\n", port);

        while (!g_exit.load(std::memory_order_relaxed))
        {
            int cli = ::accept(srv, nullptr, nullptr);
            if (cli < 0)
            {
                if (g_exit.load(std::memory_order_relaxed)) break;
                continue;
            }

            char buf[2048];
            auto const n = ::recv(cli, buf, sizeof(buf) - 1, 0);
            std::string body = R"({"error":"not found"})";
            int code = 404;
            if (n > 0)
            {
                buf[n] = '\0';
                std::istringstream is{std::string{buf}};
                std::string method;
                std::string target;
                is >> method >> target;
                if (method == "POST" && target.rfind("/start", 0) == 0)
                    body = start_session(query_param(target, "flow"),
                        query_param(target, "channels"), code);
                else if (method == "DELETE" && target.rfind("/stop", 0) == 0)
                    body = stop_session(query_param(target, "flow"), code);
                else if (method == "GET" && target.rfind("/status", 0) == 0)
                {
                    body = status_json();
                    code = 200;
                }
                else if (method == "GET" && target.rfind("/healthz", 0) == 0)
                {
                    body = R"({"ok":true})";
                    code = 200;
                }
            }

            std::ostringstream resp;
            resp << "HTTP/1.0 " << code << (code == 200 ? " OK" : " Error") << "\r\n"
                 << "Content-Type: application/json\r\n"
                 << "Access-Control-Allow-Origin: *\r\n"
                 << "Cache-Control: no-store\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n\r\n"
                 << body;
            auto const out = resp.str();
            ::send(cli, out.data(), out.size(), MSG_NOSIGNAL);
            ::close(cli);
        }
        ::close(srv);
    }
}  // namespace

int main(int argc, char** argv)
{
    ::gst_init(&argc, &argv);
    prefer_mpeg4_generic_aac();
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    g_domain = env_or("MXL_DOMAIN", "/run/mxl/domain");
    g_rtspBase = env_or("MXL_PREVIEW_RTSP_BASE", "rtsp://mediamtx:8554/preview-audio-");
    g_maxSessions = std::max(env_int("MXL_MAX_SESSIONS", 4), 1);
    g_opusBitrate = env_int("MXL_OPUS_BITRATE", 128000);
    g_aacBitrate = env_int("MXL_AAC_BITRATE", 128000);
    int const port = env_int("MXL_CONTROL_PORT", 8090);

    g_print("mxl-audio-preview: domain=%s rtspBase=%s maxSessions=%d opusBitrate=%d aacBitrate=%d\n",
        g_domain.c_str(), g_rtspBase.c_str(), g_maxSessions, g_opusBitrate, g_aacBitrate);

    serve(port);

    // SIGTERM: stop every session before exiting so each pipeline sends EOS and
    // mediamtx drops the path, instead of leaving stale ones behind.
    std::vector<std::unique_ptr<Session>> draining;
    {
        std::lock_guard<std::mutex> lk{g_mu};
        for (auto& [flow, ses] : g_sessions) draining.push_back(std::move(ses));
        g_sessions.clear();
    }
    for (auto& ses : draining)
    {
        ses->stop.store(true, std::memory_order_relaxed);
        if (ses->worker.joinable()) ses->worker.join();
    }
    g_print("mxl-audio-preview: exited\n");
    return 0;
}
