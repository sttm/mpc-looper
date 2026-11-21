FROM debian:bookworm
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends g++ g++-arm-linux-gnueabihf g++-aarch64-linux-gnu binutils-arm-linux-gnueabihf binutils-aarch64-linux-gnu python3 python3-pil make file ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /work
