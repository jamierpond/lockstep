#!/bin/sh
# Fails when a build log holds a linker or archiver warning: Apple's ld and
# ranlib, GNU ld, or MSVC's link.exe and lib.exe (LNK4xxx).
set -eu
if grep -E "ld: warning|ranlib: warning|/ld: warning|warning LNK4[0-9]+" "$1"; then
    echo "check-link-warnings: the build above has linker or archiver warnings" >&2
    exit 1
fi
