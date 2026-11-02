#!/bin/sh
# Builds and tests a fully static musl lockstep inside Alpine, so the binary
# runs on any Linux without depending on the host's glibc version. libcurl is
# built here from source against static mbedTLS, since Alpine's libcurl.a
# pulls in OpenSSL, nghttp2 and the rest.
# Run from the repository root:
#   docker run --rm -v "$PWD":/w -w /w alpine:3.22 scripts/build-linux-static.sh
set -eu
set -o pipefail

curl_version=8.23.0
curl_sha256=e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
curl_prefix=/opt/curl-static

apk add --no-cache build-base cmake ninja git curl linux-headers file binutils \
    perl mbedtls-dev mbedtls-static ca-certificates
git config --global --add safe.directory '*'

work="$(mktemp -d)"
curl -fsSL -o "$work/curl.tar.gz" "https://curl.se/download/curl-$curl_version.tar.gz"
echo "$curl_sha256  $work/curl.tar.gz" | sha256sum -c -
tar -xzf "$work/curl.tar.gz" -C "$work"

cmake -G Ninja -S "$work/curl-$curl_version" -B "$work/curl-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$curl_prefix" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
    -DBUILD_CURL_EXE=OFF -DBUILD_TESTING=OFF -DBUILD_LIBCURL_DOCS=OFF \
    -DBUILD_MISC_DOCS=OFF -DENABLE_CURL_MANUAL=OFF \
    -DHTTP_ONLY=ON -DCURL_USE_MBEDTLS=ON -DCURL_USE_OPENSSL=OFF \
    -DUSE_NGHTTP2=OFF -DUSE_LIBIDN2=OFF -DCURL_USE_LIBPSL=OFF \
    -DCURL_USE_LIBSSH2=OFF -DCURL_ZLIB=OFF -DCURL_BROTLI=OFF -DCURL_ZSTD=OFF \
    -DCURL_CA_BUNDLE=/etc/ssl/certs/ca-certificates.crt -DCURL_CA_PATH=none
cmake --build "$work/curl-build"
cmake --install "$work/curl-build"

cmake -G Ninja -B build-static -DCMAKE_BUILD_TYPE=Release -DLOCKSTEP_FULLY_STATIC=ON \
    -DCMAKE_PREFIX_PATH="$curl_prefix" -DCURL_USE_STATIC_LIBS=ON
cmake --build build-static 2>&1 | tee build-static/build.log
scripts/check-link-warnings.sh build-static/build.log
ctest --test-dir build-static --output-on-failure
scripts/check-floor.sh build-static/lockstep
