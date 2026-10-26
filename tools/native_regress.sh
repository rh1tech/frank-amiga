#!/usr/bin/env bash
# native_regress.sh <out.txt> — run the native build over a fixed set of
# disks/ROMs and record a hash of every dumped frame. Compare two outputs to
# prove a change left emulation bit-identical.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:?usage: native_regress.sh out.txt}"
BIN=build/native/omega-native
R=assets/rom/aros
: > "$OUT"
run() {   # name adf iterations dump_every
    local d; d=$(mktemp -d)
    (cd "$d" && OMEGA_ROM="$OLDPWD/$R/aros-amiga-m68k-rom.bin" \
        OMEGA_EXT_ROM="$OLDPWD/$R/aros-amiga-m68k-ext.bin" \
        "$OLDPWD/$BIN" "$OLDPWD/$2" "$3" "$4" 3000 >/dev/null 2>&1 || true)
    for f in "$d"/frame*.ppm; do echo "$1 $(basename "$f") $(shasum "$f" | cut -c1-16)"; done >> "$OUT"
    rm -rf "$d"
}
# (DiagROM run omitted: needs its own ROM)
run sota assets/adf/Spaceballs-StateOfTheArt.adf 3000000 250000
run hardwired assets/adf/Crionics-HardwiredA.adf 3000000 250000
run enigma assets/adf/PHENOMENA-Enigma.adf 3000000 250000
run desert assets/adf/Kefrens-DesertDreamA.adf 3000000 250000
wc -l < "$OUT"
