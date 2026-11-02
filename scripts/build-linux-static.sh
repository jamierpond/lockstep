#!/bin/sh
# Builds and tests a fully static musl lockstep inside Alpine, so the binary
# runs on any Linux kernel without depending on the host's glibc version.
# Run from the repository root: docker run --rm -v "$PWD":/w -w /w alpine:3.22 scripts/build-linux-static.sh
set -eu
apk add --no-cache build-base cmake ninja git curl curl-dev linux-headers file binutils
git config --global --add safe.directory '*'
cmake -G Ninja -B build-static -DCMAKE_BUILD_TYPE=Release -DLOCKSTEP_FULLY_STATIC=ON
cmake --build build-static
ctest --test-dir build-static --output-on-failure
scripts/check-floor.sh build-static/lockstep
