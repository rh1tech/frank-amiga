#!/usr/bin/env bash
# build.sh [master|slave|all] — build one or both halves with the same clocks.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
cd "$(dirname "$0")"
: "${PICO_SDK_PATH:=$HOME/pico/pico-sdk}"
export PICO_SDK_PATH
what="${1:-all}"
for half in master slave; do
    [ "$what" = all ] || [ "$what" = "$half" ] || continue
    [ -f "build/$half/build.ninja" ] || \
        cmake -S "$half" -B "build/$half" -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "build/$half" 2>&1 | grep -E "error|warning:|FAILED" || true
    test -f "build/$half/frank-amiga-$half.elf"
    arm-none-eabi-size "build/$half/frank-amiga-$half.elf" | tail -1 | \
        awk -v h="$half" '{printf "%-6s text %d  bss %d\n", h, $1, $3}'
done
