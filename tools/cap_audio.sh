#!/usr/bin/env bash
# cap_audio.sh <out.wav> <seconds> — record from the audio capture device.
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
source "$(dirname "$0")/bench.conf"
ffmpeg -hide_banner -loglevel error -y -f avfoundation -i ":$CAPTURE_AUDIO" \
    -t "${2:-4}" -ac 1 -ar 48000 "${1:?usage: cap_audio.sh out.wav seconds}"
