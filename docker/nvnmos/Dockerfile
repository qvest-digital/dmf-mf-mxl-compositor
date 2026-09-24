# libnvnmos, NVIDIA's C API over nmos-cpp: the NMOS Node the compositor links.
#
# Built apart from the compositor because its dependencies -- nmos-cpp, Boost,
# cpprestsdk, OpenSSL and the rest -- are compiled from source by Conan, which
# takes the better part of an hour on a CI runner. The result is published as
# its own image, tagged by NVNMOS_REF, and only rebuilt when that changes.
#
# The dependencies are linked statically into libnvnmos.so, which then needs
# nothing beyond libc and libstdc++, so the image carries that one file.
#
# Debian trixie, the release the go-mxl runtime image is based on, so the
# library resolves against the same libc and libstdc++ it runs with.

ARG NVNMOS_REF

FROM debian:trixie AS build

ARG NVNMOS_REF
ENV DEBIAN_FRONTEND=noninteractive PIP_BREAK_SYSTEM_PACKAGES=1
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && pip install conan~=2.2 \
    && conan profile detect

# The commit pins libnvnmos, and its conan.lock pins every dependency. The
# patches change NvNmos, not what it builds against.
COPY patches /patches
RUN test -n "${NVNMOS_REF}" \
 && git clone https://github.com/NVIDIA/nvnmos.git /nvnmos \
 && git -C /nvnmos checkout --quiet "${NVNMOS_REF}" \
 && for p in /patches/*.patch; do git -C /nvnmos apply --verbose "$p"; done
WORKDIR /nvnmos
RUN conan install src \
        --settings:all build_type=Release \
        --build=missing \
        --output-folder=src/conan \
        --lockfile=src/conan.lock \
        -o '*:shared=False' \
 && cmake -B build \
        -DCMAKE_TOOLCHAIN_FILE=conan/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_SHARED_LIBS=ON \
        -DNVNMOS_BUILD_EXAMPLES=OFF \
        src \
 && cmake --build build --parallel \
 && cmake --install build --prefix /opt/nvnmos \
 && cp LICENSE /opt/nvnmos/LICENSE

FROM scratch
COPY --from=build /opt/nvnmos /opt/nvnmos
