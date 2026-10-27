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
/amiga/kick/                Kickstart images (your own licensed copies, or AROS)
/amiga/adf/                 disk images
```

At power-on the master uploads the configured ROMs and disks to the slave and
starts the machine. Disks are writable: tracks a program writes are saved back
into the ADF file on the card about a second later.

## Controls

- USB keyboard and mouse on the master's USB port. F12 opens the menu:
  DF0/DF1 disk browser, Kickstart, reset, volume, keyboard joystick.
- Joysticks: USB gamepad 1 is the joystick in port 2 (games), gamepad 2 the
  one in port 1; d-pad or left stick, A fire, B second button. Without a pad,
  `joykeys` makes the arrows (Right Ctrl/Alt fire) or the numpad (0/. fire)
  port 2's joystick.
- Console (UART0, 115200): `df0`/`df1 <path|none>` change disks, `kick`/`ext
  <path>` change ROMs and cold-start, `boot` rereads the ini, `reset` is
  Ctrl-Amiga-Amiga, `type`/`key`/`mouse`/`joy` drive input, `osd`/`osdkey`,
  `vol`, `stats`.

## Status

- Both halves at 504 MHz; HDMI 640x480@50 locked to the emulated PAL frame,
  Paula on the I2S DAC, microSD, USB HID.
- Chipset: OCS with dual playfield, interlace (woven), collisions, audio
  attach, overscan-aware vertical window, two floppy drives with writes.
- Blits are computed at once but end (BBUSY, interrupt) after their real
  duration.

See `docs/bringup.md`.
