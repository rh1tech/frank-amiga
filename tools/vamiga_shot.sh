#!/bin/zsh
# vamiga_shot.sh <rom> <adf> <seconds> <out.png> [click-at-seconds] — render a
# reference frame with vAmiga (cycle-exact, MPL-2.0; VAHeadless built from
# github.com/dirkwhoffmann/vAmiga, path in $VAMIGA): an A500 (OCS, 512 KB chip
# + 512 KB slow), the disk in DF0, a left click at `click` seconds (Guardian
# Kickstarts ask for one; default 7), the frame at `seconds` as a PNG.
set -eu
rom=${1:A}; adf=${2:A}; secs=$3; out=${4:A}; click=${5:-7}
V=${VAMIGA:?set VAMIGA to the VAHeadless binary}
tag=vshot_$$
script=$(mktemp -t vshot).retrosh
cat > $script <<EOS
amiga init A500_OCS_1MB
mem load rom $rom
df0 insert $adf
amiga set WARP_MODE ALWAYS
amiga power on
wait $click
mouse1 press left
wait $(( secs - click ))
screenshot save $tag
EOS
$V $script >/dev/null 2>&1 || true
python3 - "/tmp/$tag.raw" "$out" <<'EOP'
import sys
from PIL import Image
data = open(sys.argv[1], 'rb').read()
w = 912 - 4 * 0x31
h = len(data) // (3 * w)
Image.frombytes('RGB', (w, h), data[:w * h * 3]).save(sys.argv[2])
print(sys.argv[2], w, h)
EOP
rm -f /tmp/$tag.raw $script
