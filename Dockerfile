# Builds the compositor and its companion binaries against the libmxl and
# libfabric pair shipped by the go-mxl base images, which carry libfabric and
# rdma-core built for broad provider coverage (efa, verbs, tcp) under /usr/lib
# alongside libmxl and libmxl-fabrics.
#
# GO_MXL_TAG pins both the build and the runtime base. MXL's domain protocol
# requires every reader and writer sharing a domain to load a byte-identical
# libmxl.so; a mismatch surfaces as MXL_ERR_UNKNOWN from mxlCreateFlowReader,
# or as grains that are present but read as garbage. A bump that changes
# /opt/libmxl/lib/libmxl.so* breaks every other process on the same domain
# until it loads the same library; compare the library of the old and new
# runtime image before accepting one.
# renovate: datasource=docker depName=ghcr.io/qvest-digital/go-mxl-builder
ARG GO_MXL_TAG=1.1.0-rc.15

# NvNmos, NVIDIA's C API over nmos-cpp, provides the NMOS Node: IS-04
# registration, the IS-05 Connection API and BCP-007-03's MXL transport. It is
# built by docker/nvnmos/Dockerfile into its own image, tagged by the NvNmos
# commit and the patches applied to it, because its dependencies take the
# better part of an hour to compile. The build workflow publishes that image
# when the tag is missing, and refuses a NVNMOS_PATCHES that is not the first
# 8 hex digits of sha256sum docker/nvnmos/patches/*.patch | sha256sum.
ARG NVNMOS_REF=36fb7db9b5616a26f77164de735c15171e01aaed
ARG NVNMOS_PATCHES=713d795a
ARG NVNMOS_IMAGE=ghcr.io/qvest-digital/dmf-mf-mxl-compositor/nvnmos:${NVNMOS_REF}-${NVNMOS_PATCHES}

# libmxl-monitoring counts what every tile reads and renders it for /metrics;
# built with the compositor and linked statically. Checked out by commit,
# which a moved tag cannot change; the tag names the release it is.
# renovate: datasource=github-tags depName=marquies/libmxl-monitoring
ARG MONITORING_TAG=v0.6.0
ARG MONITORING_REF=449f446ca2ce341e3a885bce6c204fe071870253

FROM ${NVNMOS_IMAGE} AS nvnmos

# The libmxl-monitoring source, as a stage of its own so a local build can
# take a checkout instead: --build-context monitoring=<directory>.
FROM ghcr.io/qvest-digital/go-mxl-builder:${GO_MXL_TAG} AS monitoring-clone
ARG MONITORING_REF
RUN git clone https://github.com/marquies/libmxl-monitoring.git /libmxl-monitoring \
 && git -C /libmxl-monitoring checkout "${MONITORING_REF}"

FROM scratch AS monitoring
COPY --from=monitoring-clone /libmxl-monitoring /

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
        libcurl4-openssl-dev \
        nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

# picojson, which libmxl-monitoring parses flow definitions with, from the
# image's vcpkg ports in classic mode.
RUN /root/vcpkg/vcpkg install picojson --x-install-root=/opt/vcpkg

WORKDIR /src
COPY CMakeLists.txt /src/
COPY src /src/src/
COPY tests/domain_test.cpp tests/connections_test.cpp tests/next_grain_test.cpp tests/flow_def_test.cpp tests/grain_rate_test.cpp tests/host_name_test.cpp tests/monitors_test.cpp /src/tests/
COPY --from=nvnmos /opt/nvnmos /opt/nvnmos
COPY --from=monitoring / /src/libmxl-monitoring/

# The builder image ships libmxl under /opt/libmxl/lib without a multiarch
# symlink in /usr/lib/x86_64-linux-gnu, so `-lmxl` does not resolve on its
# own. Whether the symlink is present varies by base tag; creating it here
# keeps the link line working across them.
RUN ln -s /opt/libmxl/lib/libmxl.so /usr/lib/x86_64-linux-gnu/libmxl.so \
 && ln -s /opt/libmxl/lib/libmxl-fabrics.so /usr/lib/x86_64-linux-gnu/libmxl-fabrics.so

RUN cmake -S /src -B /src/build -DCMAKE_BUILD_TYPE=Release -DNVNMOS_ROOT=/opt/nvnmos \
        -DCMAKE_PREFIX_PATH="/opt/libmxl;/opt/vcpkg/x64-linux" \
 && cmake --build /src/build --parallel \
 && ctest --test-dir /src/build --output-on-failure \
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
        libcurl4t64 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /opt/mxl-compositor/bin/mxl-multi-compositor /usr/bin/
COPY --from=nvnmos /opt/nvnmos/lib/libnvnmos.so /usr/lib/x86_64-linux-gnu/

WORKDIR /home/mxl
ENTRYPOINT ["/usr/bin/mxl-multi-compositor"]
