# ── Stage 1: build ────────────────────────────────────────────────────────────
FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential ninja-build git curl zip unzip \
        pkg-config libssl-dev zlib1g-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Debian Bookworm ships CMake 3.25; kcp's CMakeLists.txt requires 3.31+.
# Download the official Linux binary from cmake.org instead.
RUN curl -fsSL https://github.com/Kitware/CMake/releases/download/v3.31.7/cmake-3.31.7-linux-x86_64.sh \
        -o /tmp/cmake-install.sh && \
    sh /tmp/cmake-install.sh --skip-license --prefix=/usr/local && \
    rm /tmp/cmake-install.sh

# vcpkg
ENV VCPKG_ROOT=/vcpkg
RUN git clone --depth=1 https://github.com/microsoft/vcpkg ${VCPKG_ROOT} && \
    ${VCPKG_ROOT}/bootstrap-vcpkg.sh -disableMetrics

# Install only what the server needs (no Kinect, no OpenCV)
RUN ${VCPKG_ROOT}/vcpkg install ixwebsocket --triplet x64-linux

# Copy source
WORKDIR /app
COPY . .

# Build
RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake \
    && cmake --build build --config Release -j$(nproc)

# ── Stage 2: runtime ──────────────────────────────────────────────────────────
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/server /usr/local/bin/holovisualize-server

EXPOSE 8080 8081/udp 8082

ENTRYPOINT ["holovisualize-server"]
CMD ["8080"]
