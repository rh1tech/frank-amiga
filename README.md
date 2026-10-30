# frank-amiga

Amiga 500 emulator for the FRANK Core 2 Proto board (two RP2350s).

- **Slave** (RP2350A): 68000, Agnus (DMA slots, copper, blitter), CIAs, Paula timing.
- **Master** (RP2350B): Denise rendering, HDMI (HSTX), I2S audio, microSD, USB keyboard/mouse.

Built on [Omega](https://github.com/h5n1xp/Omega) by way of
[fboldog/rp2350-amiga](https://github.com/fboldog/rp2350-amiga) (MPL-2.0).

## Build and flash

```sh
./build.sh                       # both halves (same clk_sys: 504 MHz)
tools/flash.sh slave  build/slave/frank-amiga-slave.elf
tools/flash.sh master build/master/frank-amiga-master.elf
python3 tools/uart.py master send "help\r" --until "^OK"
```

Bench instruments (probes, chip IDs, capture devices) are in `tools/bench.conf`.

Testing: `tools/native_regress.sh out.txt` hashes native frames (compare two
runs to prove a change neutral); `tools/hw_suite.sh` boots each demo of the SD
card's test set on the board and reports fps, last PC and hang/reset flags
(`tools/hw_triage.sh disk.adf` for one).

## microSD layout

```
/amiga/frank-amiga.ini      kick=/amiga/kick/kick13.rom
                            ext=/amiga/kick/aros-ext.bin   (optional, AROS)
                            df0=/amiga/adf/game.adf        (optional)
                            df1=/amiga/adf/game-2.adf      (optional; connects DF1)
                            drives=2                       (DF1 connected, empty)
                            joykeys=arrows                 (off | arrows | numpad)
                            video=pal                      pal | ntsc (60 Hz, 263 lines)
                            chip=512                       chip RAM KB: 512 | 1024 | 2048
                            slow=512                       slow RAM KB at $C00000: 0 | 512 | 1024
/amiga/kick/                Kickstart images (your own licensed copies, or AROS)
/amiga/adf/                 disk images: .adf, .scp flux images and .ipf (SPS/CAPS)
                            images (copy protections such as Rob Northen Copylock
                            work from the last two)
```

At power-on the master uploads the configured ROMs and disks to the slave and
starts the machine. Disks are writable: tracks a program writes are saved back
into the ADF file on the card about a second later.

## Controls

- USB keyboard and mouse on the master's USB port. F11 opens the file
  manager: walk the card, put a disk image (.adf/.scp/.ipf, marked `*`) into DF0
  or DF1 or boot from it, pick a Kickstart (`K`), and optionally make the
  choice the power-on one (written to frank-amiga.ini). F12 opens the menu:
  DF0/DF1 disk browser, Kickstart, reset, save/load state, volume, keyboard
  joystick, floppy speed. The Amiga's Help key is on Insert.
- Joysticks: USB gamepad 1 is the joystick in port 2 (games), gamepad 2 the
  one in port 1; d-pad or left stick, A fire, B second button. Without a pad,
  `joykeys` makes the arrows (Right Ctrl/Alt fire) or the numpad (0/. fire)
  port 2's joystick.
- Console (UART0, 115200): `df0`/`df1 <path|none>` change disks, `kick`/`ext
  <path>` change ROMs and cold-start, `boot` rereads the ini, `reset` is
  Ctrl-Amiga-Amiga, `type`/`key`/`mouse`/`joy` drive input, `fm`/`osd`/`osdkey`,
  `vol`, `mem <chip KB> <slow KB>` (RAM, cold-start), `floppy turbo|real`,
  `stats`.
- Floppy speed: turbo by default (disk DMA reads as fast as it asks). With
  `floppy=real` (ini, console `floppy real` or the F12 menu; applied at once)
  disks turn at their real speed, 2 us a bit, ~0.4 s a track: loading takes
  as long as on an A500, and demos that show something while they load keep
  their timing.

## Status

- Both halves at 504 MHz; HDMI 640x480@50 locked to the emulated PAL frame
  (640x480@60 for an NTSC machine: `video=ntsc`, console `standard ntsc` or
  the F12 menu; a change restarts the board),
  Paula on the I2S DAC, microSD, USB HID.
- Machine: an A500 by default (512 KB chip + 512 KB slow RAM, smaller chip
  sizes mirroring as Agnus does), up to 2 MB chip / 1 MB slow.
- Chipset: OCS with dual playfield, interlace (woven), collisions, audio
  attach, overscan-aware vertical window, two floppy drives with writes.
- Blits are computed at once but end (BBUSY, interrupt) after their real
  duration.

See `docs/bringup.md`.

## License

frank-amiga is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version (`LICENSE`). Files marked `SPDX-License-Identifier: GPL-3.0-or-later`
are this project's own.

It includes code under other, GPL-compatible licenses; those files keep their
own terms and notices:

| Part | Origin | License |
|---|---|---|
| `third_party/omega/` (except Musashi), `native/`, files marked MPL-2.0 or derived from rp2350-amiga (`slave/src/platform/`, `master/src/denise/`, `common/amiga/`) | Omega by Matt Parsons, via [fboldog/rp2350-amiga](https://github.com/fboldog/rp2350-amiga) | MPL-2.0 |
| `third_party/omega/m68k*` | Musashi 3.4, Karl Stenerud | MIT-style (see file headers) |
| `master/lib/fatfs/` | FatFs, ChaN | BSD-style (see `ff.c`) |
| `master/lib/sdcard/` | Elehobica | BSD-2-Clause (`master/lib/sdcard/LICENSE`) |
| `master/src/usb/` (HID, XInput) | usb64, Ryan Wendland and others | MIT |
| `master/src/osd_font.h` | xrip/pico-nes | GPL-3.0 |

Kickstart ROMs and Amiga software are not part of this project; use your own
licensed copies (or AROS).
