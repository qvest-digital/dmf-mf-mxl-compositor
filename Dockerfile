# Builds the compositor and its companion binaries against the libmxl and
# libfabric pair shipped by the go-mxl base images, which carry libfabric and
# rdma-core built for broad provider coverage (efa, verbs, tcp) under /usr/lib
# alongside libmxl and libmxl-fabrics.
#
# GO_MXL_TAG pins both the build and the runtime base. MXL's domain protocol
# requires every reader and writer sharing a domain to load a byte-identical
# libmxl.so; a mismatch surfaces as MXL_ERR_UNKNOWN from mxlCreateFlowReader,
# or as grains that are present but read as garbage. Keep this equal to the
# tag the gateway and node agent serving the same domain were built from.
# Moving it here alone breaks reads against them.
# renovate: datasource=docker depName=ghcr.io/qvest-digital/go-mxl-builder
ARG GO_MXL_TAG=1.0.0-rc.12

# -- Stage 1: build the compositor binary ------------------------------------
FROM ghcr.io/qvest-digital/go-mxl-builder:${GO_MXL_TAG} AS build

USER 0:0
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        pkg-config \
        libgstreamer1.0-dev \
        libgstreamer-plugins-base1.0-dev \
        libglib2.0-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt /src/
COPY vendor /src/vendor/
COPY src /src/src/

# The builder image ships libmxl under /opt/libmxl/lib without a multiarch
# symlink in /usr/lib/x86_64-linux-gnu, so `-lmxl` does not resolve on its
# own. Whether the symlink is present varies by base tag; creating it here
# keeps the link line working across them.
RUN ln -s /opt/libmxl/lib/libmxl.so /usr/lib/x86_64-linux-gnu/libmxl.so \
 && ln -s /opt/libmxl/lib/libmxl-fabrics.so /usr/lib/x86_64-linux-gnu/libmxl-fabrics.so

RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build /src/build --parallel \
 && cmake --install /src/build --prefix /opt/mxl-compositor

# -- Stage 2: slim runtime on the matching runtime image --------------------
FROM ghcr.io/qvest-digital/go-mxl-runtime:${GO_MXL_TAG}

USER 0:0
ENV DEBIAN_FRONTEND=noninteractive
# Pipeline plugins: compositor (good), x264enc (ugly), mpegtsmux (bad),
# rtspclientsink (bad / rs-rtsp). libav for hardened muxer support.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-plugins-bad \
        gstreamer1.0-plugins-ugly \
        gstreamer1.0-libav \
        gstreamer1.0-rtsp \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/mxl-compositor/bin/mxl-multi-compositor /usr/bin/
# Companion entry point in the same image: the audio-flow preview publisher,
# selected by overriding the container command. The compositor stays default.
COPY --from=build /opt/mxl-compositor/bin/mxl-audio-preview /usr/bin/

WORKDIR /home/mxl
ENTRYPOINT ["/usr/bin/mxl-multi-compositor"]
