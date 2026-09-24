# dmf-mf-mxl-compositor

MXL mosaic compositor, packaged as a DMF media function image. Reads MXL video
flows zero-copy through libmxl, composites them into a single mosaic, encodes
that once, and publishes it over RTSP.

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
| `MXL_DOMAINS_DIR` | `domains` beside `MXL_DOMAIN` | Directory of further domains, one per id, a tile can be connected in |
| `MXL_COMPOSITE_OUT` | `rtsp://mediamtx:8554/composite` | RTSP publish target |
| `MXL_FRAME_WIDTH` | `1920` | Tile width, must match the flow definition |
| `MXL_FRAME_HEIGHT` | `1080` | Tile height, must match the flow definition |
| `MXL_GRID_COLS` | derived | Column count override |
| `MXL_STATS_PORT` | `9090` | Port for the `/stats.json` server |
| `MXL_TILES` | flow count | Tile count; tiles beyond `MXL_FLOW_IDS` start empty |

No tiles -- neither `MXL_FLOW_IDS` nor `MXL_TILES` -- exits non-zero. Opening a reader retries indefinitely
rather than exiting, so a flow that is not yet present does not turn into a
crash loop.

Grid geometry is computed once the flow count is known: `cols = ceil(sqrt(n))`,
`rows = ceil(n / cols)`. The output canvas is `cols * MXL_FRAME_WIDTH` by
`rows * MXL_FRAME_HEIGHT`; tiles are not downscaled.

`GET /stats.json` on `MXL_STATS_PORT` returns per-flow `fps`, `pushed`,
`missed`, `mbps` and `live`, plus `cols`, `rows`, `outW`, `outH` and
`grainBytes`. It is CORS-open.

## NMOS

With `NMOS_HOST_ADDRESS` set the compositor is an NMOS Node: it registers over
AMWA IS-04 and offers one BCP-007-03 MXL Receiver per tile over IS-05, named
`tile-0`, `tile-1`, and so on. A controller connecting a Receiver to an MXL
Sender puts that flow on the tile; disconnecting it, or connecting it with no
flow, leaves the tile black. A tile started from `MXL_FLOW_IDS` is reported as
already connected to that flow.

The Receivers are served under IS-05 v1.2, the first version that knows the
MXL transport. Each lists every domain it can read in its `mxl_domain_id`
constraint and refuses any other: the domain `MXL_DOMAIN` names, then each
directory under `MXL_DOMAINS_DIR` named by a domain id whose `domain_def.json`
carries that id, as BCP-007-03 lays the file out. A tile reads the connected
flow in the domain it was connected in. The list is read when the Node starts,
so a domain created later is connectable after a restart. The Node is not
started until at least one domain yields a UUID: an MXL Receiver with no domain
to name is one no controller can route to. It retries every ten seconds and
logs why; the mosaic runs meanwhile.

A flow whose frame size is not the tile's is not shown, because tiles are
composited at their native size without scaling. The tile stays black and the
reason is logged.

| Variable | Default | Meaning |
|---|---|---|
| `NMOS_HOST_ADDRESS` | unset, NMOS off | Address the Node APIs are reached at |
| `NMOS_SEED` | none, required with NMOS | Seed for resource ids; stable per instance |
| `NMOS_HTTP_PORT` | library default | Port for the Node and Connection APIs |
| `NMOS_LABEL` | `MXL compositor` | Node and Device label |
| `NMOS_DESCRIPTION` | `MXL mosaic compositor` | Node and Device description |
| `NMOS_REGISTRY_HOST` | unset, DNS-SD | Fixed IS-04 Registration API; disables discovery |
| `NMOS_REGISTRY_PORT` | `80` | Its port |
| `NMOS_SYSTEM_HOST` | `NMOS_REGISTRY_HOST` | Fixed IS-09 System API |
| `NMOS_SYSTEM_PORT` | `NMOS_REGISTRY_PORT` | Its port |
| `NMOS_DNS_DOMAIN` | from resolv.conf | DNS-SD domain to browse for a registry |

The Node is [NvNmos](https://github.com/NVIDIA/nvnmos), NVIDIA's C API over
nmos-cpp, pinned by commit in the Dockerfile's `NVNMOS_REF`.

### Conformance

    tests/nmos/run.sh

stands up an nmos-cpp registry and the compositor as a Node with Docker Compose
and runs the AMWA NMOS Testing Tool's IS-04-01, IS-05-01, IS-05-02 and
BCP-007-03-01 suites against it. A warning is reported but does not fail a
suite. It needs `/dev/shm` for the MXL domain.

## Building

    docker build -t dmf-mf-mxl-compositor .

`ARG GO_MXL_TAG` selects the `go-mxl-builder` and `go-mxl-runtime` base images.
`ARG NVNMOS_REF` selects the libnvnmos image, built from
`docker/nvnmos/Dockerfile`:

    docker build -f docker/nvnmos/Dockerfile --build-arg NVNMOS_REF=<ref> \
        -t ghcr.io/qvest-digital/dmf-mf-mxl-compositor/nvnmos:<ref> docker/nvnmos

Its dependencies are compiled from source and take the better part of an hour,
so the build workflow publishes it only when the pinned tag is missing, and the
compositor image copies the one library out of it.

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
