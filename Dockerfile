FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        bash ca-certificates curl python3 gcc libc6-dev xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Same ARM GNU release as the F4HWN firmware image. x86_64 only.
ARG ARM_GCC_VERSION=13.3.rel1
ARG ARM_GCC_SHA256=95c011cee430e64dd6087c75c800f04b9c49832cc1000127a92a97f9c8d83af4

RUN set -e; \
    TARBALL="arm-gnu-toolchain-${ARM_GCC_VERSION}-x86_64-arm-none-eabi.tar.xz"; \
    URL="https://armkeil.blob.core.windows.net/developer/Files/downloads/gnu/${ARM_GCC_VERSION}/binrel/${TARBALL}"; \
    curl -fL -o "/tmp/${TARBALL}" "${URL}"; \
    echo "${ARM_GCC_SHA256}  /tmp/${TARBALL}" | sha256sum -c -; \
    mkdir -p /opt; \
    tar -xf "/tmp/${TARBALL}" -C /opt; \
    rm "/tmp/${TARBALL}"; \
    mv "/opt/arm-gnu-toolchain-${ARM_GCC_VERSION}-x86_64-arm-none-eabi" /opt/toolchain

ENV PATH="/opt/toolchain/bin:${PATH}"
WORKDIR /work
