# Developing on the FRANK Core 2 bench

How to drive the board from the Mac: probes, consoles, flashing, video and
audio capture, input, files, debugging, and the reference emulators. Machine
facts (serials, devices) live in `tools/bench.conf`; every tool reads it.

## The board

Two RP2350s at 504 MHz, joined by an 8-bit PIO link:

| | chip | runs | SWD | console |
|---|---|---|---|---|
| **master** | RP2350B (U3) | Denise render, HDMI (HSTX), audio, SD card, USB host, menu, console | J1 | UART0 on J2 |
| **slave** | RP2350A (U6) | 68000 (core 1), chipset/CIA/Paula/disk DMA (core 0), chip RAM in PSRAM | J3 | UART1 on J4 |

Probes (always addressed by USB serial, never by `/dev` name — tty names move
on replug):

- master: official Raspberry Pi Debug Probe, `E6616407E335BB29`. **SWD at
  1 MHz only** — faster corrupts writes once HDMI runs.
- slave: a Pico running debugprobe, `E6635C08CB46BB26` (GP2 SWCLK, GP3 SWDIO,
  GP4/GP5 UART). SWD at 4 MHz.

Both chips' own USB-C ports are cabled to the Mac too (picotool, BOOTSEL by
chip ID: master `CA6828FE74C242BD`, slave `0EABFE97D5DFEA9F`).

## Build and flash

```sh
./build.sh                 # both halves (or: ./build.sh master | slave)
tools/flash.sh slave  build/slave/frank-amiga-slave.elf
tools/flash.sh master build/master/frank-amiga-master.elf
```

- `flash.sh` programs over SWD and leaves the chip running; `--usb` uses
  picotool against a chip already in BOOTSEL.
- Flash the **master first, then the slave** when both changed; give the
  master ~4 s to bring up HDMI and the link.
- Reflashing the master cold-starts the Amiga from `frank-amiga.ini`;
  reflashing only the slave makes the master re-upload ROM and disks.
- Variant builds use their own directory and `FRANK_EXTRA_DEFS`:

  ```sh
  cmake -S slave -B build/slave-prof -G Ninja -DCMAKE_BUILD_TYPE=Release \
        "-DFRANK_EXTRA_DEFS=FRANK_PROF;DMA_SLOT_STATS"
  cmake --build build/slave-prof
  ```

  `FRANK_PROF` prints per-core cycle budgets each report;
  `PAR_CPU_SHIFT=2;FRANK_A1200_TEST=1` runs a 14 MHz 68EC020.
- **SRAM is nearly full** on both chips (slave ~441 KB of .bss, the hot files
  run from RAM: see `slave/memmap_slave.ld`). A link error
  `region 'RAM' overflowed` means new hot code; put cold paths in a file that
  stays in flash (e.g. `slave/src/main.c`). The profiling build currently
  overflows by a few hundred bytes.
- `tools/flash_rom.sh` writes a raw ROM image into a chip's flash.

If a chip is wedged: `picotool reboot -f --ser <chip id>`, or a one-session
openocd `init; reset halt; resume`. A Pico probe stuck in BOOTSEL also wants
`picotool reboot`. Power-cycling is a user action.

## Consoles

```sh
python3 tools/uart.py master send "help\r" --until "^OK"
python3 tools/uart.py slave  capture --seconds 3
```

- `send` writes text and reads until `--until` matches (exit 0) or
  `--seconds` runs out (exit 1). Every master command ends with `OK <cmd>` or
  `ERR`; wait for `^OK` before the next one.
- The master prompt appears ~14 s after a master reset.
- The slave prints a status line every 2 s:
  `emu: 50.0 frames/s, busy 62%, ... pc 02c612`. `busy` is the slave's share
  of each 20 ms frame; `pc` the 68000's PC. Watching fps and PC is the
  quickest health check (same PC for many samples = hang; PC in `$FC....`
  = back in Kickstart, reset or crash).

### Master commands (the useful ones)

| command | does |
|---|---|
| `df0 "<path>"`, `df1 "<path>"` | change a disk (ADF/DMS reply `-> slave ... ok`, SCP/IPF `flux image ... inserted`); quote paths with spaces |
| `kick <path>` | change Kickstart and cold-start (`kick 0:/amiga/kick/kick34005.A500`) |
| `boot` | reload `frank-amiga.ini` and cold-start |
| `reset` | Ctrl-Amiga-Amiga |
| `mem <chip KB> <slow KB>` | RAM size, cold start |
| `standard pal\|ntsc` | video standard (saved to the ini, restarts) |
| `meter on\|off` | EMU ms / FPS overlay (also F12 menu, ini `meter=on`) |
| `state save\|load` | savestate in slave PSRAM (one slot, lost on slave reflash or rebuild) |
| `stats` | master display/link stats over one second |
| `osd`, `fm`, `osdkey <key>...` | F12 menu, F11 file manager, keys for them |
| `usb` | what the USB host sees |
| `sdput`, `sdrm` | write/delete files on the card (see Files) |

## Input (no hands on the keyboard needed)

All from the master console:

- **Typing:** `type <text>`, `\n` is Return: `type 1234\n`. Typed text goes
  at a typist's pace (100 ms hold, 150 ms gap); games that poll the
  keyboard miss faster input.
- **Raw keys:** `key <hex> [down|up|tap]` with Amiga raw keycodes: `tap`
  holds ~100 ms. Common codes: `40` Space, `44` Return, `45` Esc, `41`
  Backspace, `42` Tab, `4C/4D/4F/4E` cursor up/down/left/right, `50`–`59`
  F1–F10, `01`–`0A` digits 1–0, `60/61` Shift, `63` Ctrl, `64/65` Alt,
  `66/67` Amiga.
- **Mouse:** `mouse <dx> <dy> [buttons]`, buttons bit 0 left, bit 1 right. A
  click is `mouse 0 0 1` then `mouse 0 0 0`. Large moves accelerate: move
  to a corner first (`mouse -120 -120 0` twice) and then step from there.
- **Joystick:** `joy <port> <bits>` (1 up, 2 down, 4 left, 8 right, 16 fire,
  32 fire 2); release with `joy 1 0`. Port 1 is the game port.
- Kickstarts with the Guardian patch show a click-to-continue screen after
  every boot: click past it (`mouse 0 0 1`, `mouse 0 0 0`).

## Video and audio capture

The HDMI output goes into a USB capture card (AVFoundation `USB Video`, audio
`USB Audio Device`, matched by name — device indices renumber).

```sh
tools/cap_frame.sh shot.png 720x576 50 2     # one frame (skip 2)
tools/cap_audio.sh out.wav 4                 # 4 s mono 48 kHz
```

- The board sends 640x512p50 (PAL), 640x480p60 (NTSC), or 640x480p50 with
  ini `display=480`; the card scales to 720x576 (factor 1.125 horizontally).
- Colours are not exact: the card expands limited range. Compare shapes and
  positions, not RGB values; use on-board CRCs for bit-exact checks.
- Audio capture clips easily and its clock wanders ±2 %: fine for waveforms,
  not for pitch.
- **Halting the master's core 1 kills HDMI** (colour bars); reflash or
  resume to get it back. Never halt the master while you need the picture.

## Files on the SD card

Layout and ini keys: README, "microSD layout". Images live in `/amiga/adf/`,
the demo test set in `/amiga/demos/` (short names: `sota.adf`, `arte.adf`,
`desertdream.adf`, ...), Kickstarts in `/amiga/kick/`.

Copy a file onto the card without removing it (the master must be running):

```sh
tools/sd_put.sh local.adf /amiga/adf/local.adf
```

It stages the file in master PSRAM over SWD and saves it with `sdput`
(`sdhold` first, as the staging area doubles as the flux track cache).

## Debugging

### Reading the live machine over SWD

Non-halting reads work while the emulator runs:

```sh
openocd -f interface/cmsis-dap.cfg -f target/rp2350.cfg \
  -c "adapter serial E6635C08CB46BB26" -c "adapter speed 4000" -c init \
  -c "targets rp2350.cm0" -c "mdw <addr> <count>" -c exit
```

- Symbol addresses come from `arm-none-eabi-nm build/slave/frank-amiga-slave.elf`
  and **move with every rebuild**; struct offsets from
  `arm-none-eabi-gdb -batch -ex "ptype /o Chipset_t" <elf>`.
- `chipset` (type `Chipset_t`) mirrors the custom registers. COP1LC/COP2LC
  and BPLxPT are kept as **word** addresses (byte address = value << 1).
- Chip RAM lives in 64 KB banks, some promoted to SRAM: `chip_bank[]` holds
  each bank's pointer; read the pointers, then `dump_image` each bank.
  Bytes are in Amiga (big-endian) order.
- Slow RAM ($C00000) is a separate PSRAM region (`slow_ram`).
- To freeze a scene: halt the **slave** (`targets rp2350.cm1; halt;
  targets rp2350.cm0; halt`). The master keeps showing the last frame.
  Reflash the slave afterwards; a later openocd session cannot resume cores a
  previous session halted.
- Disassemble a RAM dump with Musashi's disassembler (the pico-amiga host
  build has `m68kdis file.bin base_hex [from [to]]`).

### Profiling

```sh
python3 tools/profile.py build/slave/frank-amiga-slave.elf 2000 0   # core 0
python3 tools/profile.py build/slave/frank-amiga-slave.elf 2000 1   # core 1 (68000)
PROBE=E6616407E335BB29 SWD_KHZ=1000 python3 tools/profile.py build/master/frank-amiga-master.elf 1500 1
tools/profile_lines.py <elf> 4000                                   # per source line
```

Non-halting PC sampling (DWT_PCSR). Profile while the slow part is on
screen: watch the slave's fps line and start sampling when it drops.

### Native build and regressions

`native/` runs the same chipset/CPU code on the Mac (no link, no master
renderer): see `native/README.md`. Before committing an emulation change:

```sh
native/build.sh
tools/native_regress.sh new.txt     # compare with a saved baseline: must be identical
```

The board suite: `tools/hw_suite.sh` (all demos), `tools/hw_triage.sh disk.adf`
(one), `tools/fps_run.sh` (fps log).

## Reference emulators

- **vAmiga** (cycle-exact, the picture reference). Build `VAHeadless` from
  github.com/dirkwhoffmann/vAmiga (`Core/`, cmake target `VAHeadless`), then
  `VAMIGA=<bin> tools/vamiga_shot.sh kick.rom disk.adf <seconds> out.png 3`.
  One shot per run (`screenshot save` exits); run several in parallel.
  A500 OCS 512+512 KB, warp mode.
- **pico-amiga** (a teammate's single-RP2350 emulator, GPL-3.0): its host
  bench `pico-amiga-host --rom kick.rom --slow --adf d.adf --frames N
  --shot N:f.png --key N:code:down --mouse N:dx:dy:buttons` is a fast second
  opinion. It loads ADF/DMS only.
- **UAE / uae4all2** (github.com/rsn8887/uae4all2): source reference for chip
  behaviour (`custom.cpp`, `drawing.cpp`, `blitter.cpp`).
- Reference shots of a demo, scene by scene: `assets/demo/<name>/proven`
  (vAmiga) and `assets/demo/<name>/rp2350` (the board, with the meter), at
  the same demo times (`tNNN.png` = NNN s after power-on).

## Gotchas

- The Bash tool's shell is zsh: an unquoted `$var` is not word-split. Use
  functions or arrays instead of `cmd $ARGS`.
- IPF and SCP images take time to decode on first access; ADFs upload at
  `df0` time.
- A savestate taken by one build cannot be loaded by another (it is a copy
  of the emulator's RAM).
- No AI attribution lines in commits (the commit-msg hook strips them).
