#!/bin/sh
# Configures and builds another project with this checkout's build/lockstep in
# place of the release its bootstrap pins, with no --target, and lists every
# object file the build compiled.
# Usage: scripts/build-project-with-local-lockstep.sh <project-dir> <build-dir>
set -eu
project="$1"
build="$2"
lockstep="$(cd "$(dirname "$0")/.." && pwd)/build/lockstep"
if [ -f "$lockstep.exe" ]; then
    lockstep="$lockstep.exe"
fi

LOCKSTEP_EXECUTABLE="$lockstep" cmake -G Ninja -S "$project" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build "$build" -j
awk 'NR > 1 { print $4 }' "$build/.ninja_log" | grep -E '\.(o|obj)$' | sort -u
