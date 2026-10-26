#!/usr/bin/env bash
# fps_run.sh <seconds> — flash both halves (current builds), log the slave's
# emulated fps for <seconds>, print it as one line.
set -euo pipefail
cd "$(dirname "$0")/.."
secs="${1:-95}"
tools/flash.sh slave build/slave/frank-amiga-slave.elf >/dev/null
(python3 tools/uart.py slave capture --seconds "$secs" > build/slave-uart.log 2>&1 &)
tools/flash.sh master build/master/frank-amiga-master.elf >/dev/null
sleep $((secs - 3))
grep -E "HARD" build/slave-uart.log || true
grep -o "\[ *[0-9.]*\] emu: [0-9.]* frames/s" build/slave-uart.log | awk '{printf "%s%s ", $1,$2; printf "%s\n", $4}' | awk '{printf "%s ", $0} END {print ""}'
