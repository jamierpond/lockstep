#!/bin/sh
# Fails when a release binary needs a newer OS than the README promises:
# macOS 11.0 on both slices, or any shared library at all on Linux.
set -eu
binary="$1"

case "$(uname -s)" in
    Darwin)
        vtool -show-build "$binary"
        if vtool -show-build "$binary" | grep minos | grep -qv ' 11\.0$'; then
            echo "check-floor: $binary needs a macOS newer than 11.0" >&2
            exit 1
        fi
        for architecture in arm64 x86_64; do
            lipo "$binary" -verify_arch "$architecture"
        done
        ;;
    Linux)
        file "$binary"
        if ! file "$binary" | grep -q 'statically linked'; then
            echo "check-floor: $binary is not statically linked" >&2
            exit 1
        fi
        if objdump -T "$binary" 2>/dev/null | grep -q GLIBC_; then
            echo "check-floor: $binary imports glibc symbols" >&2
            exit 1
        fi
        ;;
esac
