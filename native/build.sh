#!/usr/bin/env bash
# native/build.sh — head-less desktop build of the Omega core (from
# fboldog/rp2350-amiga native/build.sh), for reference runs and screenshots.
#   VIDEO=PAL|NTSC (default PAL), HEADLESS=1 to skip SDL2.
set -euo pipefail
cd "$(dirname "$0")/.."
OUT=build/native/omega-native
mkdir -p build/native
CC=${CC:-cc}
VIDEO=${VIDEO:-PAL}
case "$VIDEO" in PAL|pal) VD=1 ;; NTSC|ntsc) VD=0 ;; *) exit 2 ;; esac
SDL_FLAGS=""; SDL_LIBS=""; SDL_OBJ=""
if [ "${HEADLESS:-1}" != "1" ] && command -v sdl2-config >/dev/null; then
    SDL_FLAGS="-DHAVE_SDL2 $(sdl2-config --cflags)"; SDL_LIBS="$(sdl2-config --libs)"
    $CC -O2 -std=gnu11 -Inative -Icommon/amiga -Ithird_party/omega -w $SDL_FLAGS \
        -DOMEGA_VIDEO_STANDARD=$VD -c -o build/native/display_sdl.o native/display_sdl.c
    SDL_OBJ=build/native/display_sdl.o
fi
O=third_party/omega
$CC -O2 -g -std=gnu11 -include native/sdl_shim.h \
    -Inative -Icommon/amiga -Islave/src/platform -I$O -w \
    $SDL_FLAGS -DOMEGA_VIDEO_STANDARD=$VD -DP2_DEBUG_BLITS ${EXTRA_CFLAGS:-} \
    -o "$OUT" native/main_native.c native/host_native.c native/memory_native.c \
    native/hdf_native.c native/kicksmash_native.c \
    slave/src/platform/Planar.c native/Presentation.c $SDL_OBJ \
    common/amiga/Collision.c $O/Audio.c $O/AudioMix.c $O/Blitter.c $O/Chipset.c $O/CIA.c $O/CPU.c \
    $O/debug.c $O/DMA.c $O/Floppy.c $O/Expansion.c $O/Gayle.c $O/Picasso2.c \
    $O/KickSmash.c $O/m68kcpu.c $O/m68kdasm.c $O/m68kopac.c $O/m68kopdm.c \
    $O/m68kopnz.c $O/m68kops.c $SDL_LIBS -lm
echo "built $OUT"
