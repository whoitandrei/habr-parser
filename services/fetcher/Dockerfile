# syntax=docker/dockerfile:1.7

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

COPY services/fetcher/conanfile.txt services/fetcher/conanfile.txt
RUN --mount=type=cache,target=/root/.conan2,sharing=locked \
    conan profile detect --force && \
    conan install services/fetcher/conanfile.txt \
        --build=missing \
        -s build_type=Release \
        -o "*:shared=False" \
        --output-folder=/app/build

COPY shared ./shared
COPY services/fetcher ./services/fetcher

RUN --mount=type=cache,target=/root/.conan2,sharing=locked \
    cmake -S services/fetcher -B build \
        -DCMAKE_TOOLCHAIN_FILE=/app/build/conan_toolchain.cmake \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j "$(nproc)"

# --- runtime ----------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /app/build/fetcher /usr/local/bin/fetcher

RUN useradd --system --create-home --shell /usr/sbin/nologin habr
USER habr

ENTRYPOINT ["/usr/local/bin/fetcher"]
