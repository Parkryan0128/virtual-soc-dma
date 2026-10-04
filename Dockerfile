FROM ubuntu:24.04@sha256:534baea6a22c03a63003dbc8dbe78fe34bc0d7e595d9a9dc9834884ff530eb55
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    build-essential git ca-certificates curl python3 python3-venv python3-pip \
    ninja-build pkg-config libglib2.0-dev libpixman-1-dev libfdt-dev zlib1g-dev \
    gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf meson \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
