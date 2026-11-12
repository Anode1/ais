# The 32-bit userland tests/abi32/run.sh builds and tests the engine in:
# int, long, size_t, pointers and time_t are all 32 bits here, as on an
# armeabi-v7a phone. Built once by run.sh as the image "ais-abi32".
FROM i386/debian:12
RUN apt-get update && \
    apt-get install -y --no-install-recommends gcc make libc6-dev curl ca-certificates && \
    rm -rf /var/lib/apt/lists/*
