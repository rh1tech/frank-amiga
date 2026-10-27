#!/bin/zsh
# hw_suite.sh [seconds] — run hw_triage.sh over the demo set on the SD card
# (scene.org releases, see docs/assets.md; DIR, default /amiga/demos) and
# list the results.
cd "$(dirname "$0")/.."
for d in sota enigma hardwired 9fingers arte interference desertdream \
         kefrens-megademoviii-a kefrens-psychoball scoopex-mentalhangover \
         andromeda-mindriot sanity-turmoil sanity-hospitalsincefriday; do
    DIR=${DIR:-/amiga/demos} tools/hw_triage.sh $d.adf ${1:-90}
done
