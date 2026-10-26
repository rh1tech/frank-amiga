#!/usr/bin/env bash
#
# flash.sh <master|slave> <image.elf> [--usb]
#
# Program one half and leave it running. SWD by default (CMSIS-DAP probe
# addressed by USB serial); --usb uses picotool against the half's own USB
# port, which must already be in BOOTSEL.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/bench.conf"

role="${1:?usage: flash.sh <master|slave> <image.elf> [--usb]}"
elf="${2:?usage: flash.sh <master|slave> <image.elf> [--usb]}"
mode="${3:-swd}"
[ -f "$elf" ] || { echo "no such image: $elf" >&2; exit 1; }

case "$role" in
    master) probe=$MASTER_PROBE; speed=$MASTER_SWD_KHZ; chip=$MASTER_CHIP_ID ;;
    slave)  probe=$SLAVE_PROBE;  speed=$SLAVE_SWD_KHZ;  chip=$SLAVE_CHIP_ID ;;
    *) echo "role must be master or slave" >&2; exit 2 ;;
esac

if [ "$mode" = "--usb" ]; then
    picotool load -v -x "$elf" --ser "$chip"
    exit 0
fi

# One openocd session: stop both cores in the boot ROM before anything in
# flash can run, write, verify, then restart. reset halt + resume, not a
# bare reset run (see frank-linux tools/reset.sh).
log="$(openocd -f interface/cmsis-dap.cfg -c "adapter serial $probe" \
        -c "adapter speed $speed" -f target/rp2350.cfg \
        -c init -c "reset halt" \
        -c "flash write_image erase $elf" -c "verify_image $elf" \
        -c "reset halt" -c resume -c exit 2>&1)" || true
if ! grep -qE "verified [0-9]+ bytes" <<<"$log"; then
    echo "$log" | tail -20 >&2
    echo "error: $role image did not verify" >&2
    exit 1
fi
grep -E "wrote [0-9]+ bytes|verified [0-9]+ bytes" <<<"$log" | sed 's/^/    /'
echo "==> $role running $(basename "$elf")"
