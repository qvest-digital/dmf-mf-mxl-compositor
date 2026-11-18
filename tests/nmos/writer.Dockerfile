# The lane's MXL writer: mxl-gst-testsrc from the go-mxl builder image, on the
# runtime image of the same tag, so it loads the libmxl the compositor does.
ARG GO_MXL_TAG
FROM ghcr.io/qvest-digital/go-mxl-builder:${GO_MXL_TAG} AS build

FROM ghcr.io/qvest-digital/go-mxl-runtime:${GO_MXL_TAG}
USER 0:0
RUN apt-get update && apt-get install -y --no-install-recommends \
        gstreamer1.0-plugins-base \
        gstreamer1.0-plugins-good \
        gstreamer1.0-x \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/libmxl/bin/mxl-gst-testsrc /usr/bin/
ENTRYPOINT ["/usr/bin/mxl-gst-testsrc"]
