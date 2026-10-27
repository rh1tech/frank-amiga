#!/usr/bin/env bash
# sd_put.sh <local file> <sd path> — copy a file onto the master's microSD
# without removing the card: SWD writes it into master PSRAM (0x200000), the
# console's `sdput` saves it. The master firmware must be running.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
source "$HERE/bench.conf"
f="${1:?usage: sd_put.sh file sdpath}"; dst="${2:?usage: sd_put.sh file sdpath}"
size=$(stat -f %z "$f")
# The staging area is the flux track cache: keep the master from decoding
# into it meanwhile.
python3 "$HERE/uart.py" master send "sdhold\r" --seconds 3 --until "^OK|^ERR" >/dev/null
log="$(openocd -f interface/cmsis-dap.cfg -c "adapter serial $MASTER_PROBE" \
     -c "adapter speed $MASTER_SWD_KHZ" -f target/rp2350.cfg -c init \
     -c "load_image $f 0x15200000 bin" -c exit 2>&1)" || true
grep -q "downloaded" <<<"$log" || { echo "$log" | tail -5; exit 1; }
python3 "$HERE/uart.py" master send "sdput 200000 $size $dst\r" --seconds 30 \
    --until "^OK|^ERR" | grep -E "media:|ERR"
echo "local crc $(python3 -c "import zlib,sys; print('%08x' % zlib.crc32(open(sys.argv[1],'rb').read()))" "$f") (must match the master's)"
