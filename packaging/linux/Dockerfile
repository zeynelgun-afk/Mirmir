FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends \
    dpkg-dev rpm squashfs-tools zstd ca-certificates file binutils \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /work
