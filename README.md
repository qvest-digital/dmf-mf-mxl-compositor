# dmf-mf-mxl-compositor

MXL mosaic compositor, packaged as a DMF media function image. Reads MXL video
flows zero-copy through libmxl, composites them into a single mosaic, encodes
that once, and publishes it over RTSP.

The image carries two entry points. The compositor is the default; the other is
selected by overriding the container command.

    /usr/bin/mxl-multi-compositor   mosaic compositor (ENTRYPOINT)
    /usr/bin/mxl-audio-preview      audio flow -> RTSP, on demand

Image: `ghcr.io/qvest-digital/dmf-mf-mxl-compositor`

The chart that deploys this image lives in
[`dmf-catalog`](https://github.com/qvest-digital/dmf-catalog), next to the
`MediaFunctionClass` that points at it. This repository builds the image and
nothing else.

## mxl-multi-compositor

One dedicated reader thread per flow pulls the freshest complete grain from the
MXL ring buffer at the flow's native grain rate. A GStreamer `compositor`
element lays the raw frames out at their native tile size and hands the
composed frame to a single x264 encoder, so there is no per-flow decode pass.
The encoded bitstream goes to `rtspclientsink`.

| Variable | Default | Meaning |
|---|---|---|
| `MXL_FLOW_IDS` | none, required | Space-separated flow UUIDs, one per tile |
| `MXL_DOMAIN` | `/domain` | MXL domain directory to read from |
| `MXL_COMPOSITE_OUT` | `rtsp://mediamtx:8554/composite` | RTSP publish target |
| `MXL_FRAME_WIDTH` | `1920` | Tile width, must match the flow definition |
| `MXL_FRAME_HEIGHT` | `1080` | Tile height, must match the flow definition |
| `MXL_GRID_COLS` | derived | Column count override |
| `MXL_STATS_PORT` | `9090` | Port for the `/stats.json` server |

Empty `MXL_FLOW_IDS` exits non-zero. Opening a reader retries indefinitely
rather than exiting, so a flow that is not yet present does not turn into a
crash loop.

Grid geometry is computed once the flow count is known: `cols = ceil(sqrt(n))`,
`rows = ceil(n / cols)`. The output canvas is `cols * MXL_FRAME_WIDTH` by
`rows * MXL_FRAME_HEIGHT`; tiles are not downscaled.

`GET /stats.json` on `MXL_STATS_PORT` returns per-flow `fps`, `pushed`,
`missed`, `mbps` and `live`, plus `cols`, `rows`, `outW`, `outH` and
`grainBytes`. It is CORS-open.

## mxl-audio-preview

Reads an MXL AUDIO flow's per-channel sample buffers, interleaves them to
F32LE, and publishes two RTSP paths per flow: Opus for WHEP and AAC for HLS,
the latter suffixed `-hls`. Neither transport carries the other's codec, so
both are published.

Sessions are created on demand over a control API rather than one process per
flow.

| Variable | Default | Meaning |
|---|---|---|
| `MXL_DOMAIN` | `/run/mxl/domain` | MXL domain directory to read from |
| `MXL_PREVIEW_RTSP_BASE` | `rtsp://mediamtx:8554/preview-audio-` | Path prefix to publish into |
| `MXL_CONTROL_PORT` | `8090` | Control API port |
| `MXL_MAX_SESSIONS` | `4` | Concurrent previews before 429 |
| `MXL_OPUS_BITRATE` | `128000` | Opus bitrate |
| `MXL_AAC_BITRATE` | `128000` | AAC bitrate |

    POST   /start?flow=<uuid>    start a preview session
    DELETE /stop?flow=<uuid>     stop it
    GET    /status               sessions, levels and per-session verdicts
    GET    /healthz

An AUDIO flow may declare `audio/float32` and still not carry normalised PCM.
Samples far above full scale are reported through `/status` and replaced with
silence rather than published, so a transport-integrity pattern does not become
full-scale noise on the way out.

The publish target must already exist as a path on the RTSP server: this
publishes into it, it does not create it.


## Building

    docker build -t dmf-mf-mxl-compositor .

`ARG GO_MXL_TAG` selects the `go-mxl-builder` and `go-mxl-runtime` base images
and is the only version knob.

### go-mxl lock-step

MXL's domain protocol requires every reader and writer sharing a domain to load
a byte-identical `libmxl.so`. A mismatch between this image and the gateway or
node agent serving the same domain surfaces as `MXL_ERR_UNKNOWN` from
`mxlCreateFlowReader`, or as grains that are present but read as garbage.

`GO_MXL_TAG` must therefore match the tag those components were built from.
Renovate proposes bumps against the inline marker above the `ARG` line;
accepting one in isolation is not safe.

`vendor/mxl/` carries the MXL headers the sources compile against. `libmxl.so`
itself comes from the base image at build and run time.

## Releasing

release-please runs in manifest mode with one package for the repository,
producing a `1.0.0-rc.N` prerelease series tagged `vX.Y.Z`. Publishing a
release triggers the image build, which tags the image with the version minus
the leading `v`. Every push to `main` also publishes a short-sha tag and
`latest`.
