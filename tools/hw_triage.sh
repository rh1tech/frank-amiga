#!/bin/zsh
# hw_triage.sh <disk.adf on SD, under /amiga/adf> [seconds] — boot Kickstart
# 1.3 (ini kick) with the disk in DF0, click past Guardian, then sample the
# slave's status and the HDMI capture every ~11 s. Prints one line: fps per
# sample, last PC and flags (IN-ROM: back in Kickstart, i.e. reset or crash;
# STUCK: same PC three samples running; BLANK: last frame one colour).
# Frames land in $OUT (default /tmp/hw_triage/<disk>/).
set -u
cd "$(dirname "$0")/.."
disk=${1:?usage: hw_triage.sh disk.adf [seconds]}; secs=${2:-90}
name=${disk%.adf}; OUT=${OUT:-/tmp/hw_triage}/$name; rm -rf $OUT; mkdir -p $OUT
python3 tools/uart.py master send "df0 /amiga/adf/$disk\r" --seconds 20 --until "^OK" >/dev/null
python3 tools/uart.py master send "kick\r" --seconds 3 --until "^OK" | grep -o "kick=[^ ]*" | read k
python3 tools/uart.py master send "kick ${k#kick=}\r" --seconds 25 --until "^OK" >/dev/null
sleep 7
python3 tools/uart.py master send "mouse 0 0 1\r" --seconds 2 --until "^OK" >/dev/null; sleep 0.3
python3 tools/uart.py master send "mouse 0 0 0\r" --seconds 2 --until "^OK" >/dev/null
last=""; same=0; fl=""; out=""; n=$(( secs / 11 ))
for i in $(seq 1 $n); do
  sleep 9
  tools/cap_frame.sh $OUT/f$i.png >/dev/null 2>&1
  l=$(python3 tools/uart.py slave capture --seconds 2.2 | grep "emu:" | tail -1)
  f=$(echo $l | grep -o '[0-9.]* frames/s' | cut -d' ' -f1); pc=$(echo $l | grep -o 'pc [0-9a-f]*' | cut -d' ' -f2)
  [ "$pc" = "$last" ] && same=$((same+1)) || same=0; last=$pc
  out="$out ${f:-?}"
done
[ $(( 0x${pc:-0} & 0xFFFFFF )) -ge $((0xF80000)) ] && fl="$fl IN-ROM"
[ $same -ge 3 ] && fl="$fl STUCK"
c=$(python3 -c "from PIL import Image; im=Image.open('$OUT/f$n.png').convert('RGB').resize((160,120)); print(len(set(im.getdata())))" 2>/dev/null)
[ "${c:-0}" -le 2 ] && fl="$fl BLANK"
echo "$name fps:$out pc=${pc:-?}$fl"
