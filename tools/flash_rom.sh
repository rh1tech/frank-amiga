#!/usr/bin/env bash
# flash_rom.sh <master|slave> <file.rom> [address] — write a raw binary (a
# Kickstart image) into a half's flash over SWD, verified. Default address
# 0x10200000, where the slave looks for its ROM.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/bench.conf"
role="${1:?usage: flash_rom.sh <master|slave> <file> [addr]}"
file="${2:?usage: flash_rom.sh <master|slave> <file> [addr]}"
addr="${3:-0x10200000}"
case "$role" in
    master) probe=$MASTER_PROBE; speed=$MASTER_SWD_KHZ ;;
    slave)  probe=$SLAVE_PROBE;  speed=$SLAVE_SWD_KHZ ;;
esac
log="$(openocd -f interface/cmsis-dap.cfg -c "adapter serial $probe" \
        -c "adapter speed $speed" -f target/rp2350.cfg -c init -c "reset halt" \
        -c "flash write_image erase $file $addr bin" -c "verify_image $file $addr bin" \
        -c "reset halt" -c resume -c exit 2>&1)" || true
grep -qE "verified [0-9]+ bytes" <<<"$log" || { echo "$log" | tail -15 >&2; exit 1; }
grep -E "wrote|verified" <<<"$log" | sed 's/^/    /'
