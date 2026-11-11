#!/bin/sh
# Builds and tests a fully static musl lockstep inside Alpine, so the binary
# runs on any Linux without depending on the host's glibc version. libcurl and
# mbedTLS come from lockstep.lock, as in every Linux build.
# Run from the repository root:
#   docker run --rm -v "$PWD":/w -w /w alpine:3.22 scripts/build-linux-static.sh
set -eu
set -o pipefail

apk add --no-cache build-base cmake ninja git git-lfs patch linux-headers file binutils \
    ca-certificates
git config --global --add safe.directory '*'

cmake -G Ninja -B build-static -DCMAKE_BUILD_TYPE=Release -DLOCKSTEP_FULLY_STATIC=ON
cmake --build build-static 2>&1 | tee build-static/build.log
scripts/check-link-warnings.sh build-static/build.log
ctest --test-dir build-static --output-on-failure
scripts/check-floor.sh build-static/lockstep
