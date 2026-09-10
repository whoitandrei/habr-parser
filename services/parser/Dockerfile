# syntax=docker/dockerfile:1.7
#
# Build context is the repository root — see services/fetcher/Dockerfile for the
# reasoning behind the base image and the shared Conan cache mount.

# --- build ------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        python3 \
        python3-pip \
        pkg-config \
        perl \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --no-cache-dir "conan==2.32.0"

WORKDIR /app

# "conan profile detect" runs inside the mounted RUN on purpose — see
# services/fetcher/Dockerfile.
COPY services/parser/conanfile.txt services/parser/conanfile.txt
RUN --mount=type=cache,target=/root/.conan2,sharing=locked \
    conan profile detect --force && \
    conan install services/parser/conanfile.txt \
        --build=missing \
        -s build_type=Release \
        -o "*:shared=False" \
        --output-folder=/app/build

COPY shared ./shared
COPY services/parser ./services/parser

RUN --mount=type=cache,target=/root/.conan2,sharing=locked \
    cmake -S services/parser -B build \
        -DCMAKE_TOOLCHAIN_FILE=/app/build/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j "$(nproc)"

# --- runtime ----------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

COPY --from=builder /app/build/parser /usr/local/bin/parser

RUN useradd --system --create-home --shell /usr/sbin/nologin habr
USER habr

ENTRYPOINT ["/usr/local/bin/parser"]
