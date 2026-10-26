# frank-amiga

Amiga 500 emulator for the FRANK Core 2 Proto board (two RP2350s).

- **Slave** (RP2350A): 68000, Agnus (DMA slots, copper, blitter), CIAs, Paula timing.
- **Master** (RP2350B): Denise rendering, HDMI (HSTX), I2S audio, microSD, USB keyboard/mouse.

Built on [Omega](https://github.com/h5n1xp/Omega) by way of
[fboldog/rp2350-amiga](https://github.com/fboldog/rp2350-amiga) (MPL-2.0).

## Build and flash

```sh
./build.sh                       # both halves (same clk_sys: 528 MHz)
tools/flash.sh slave  build/slave/frank-amiga-slave.elf
tools/flash.sh master build/master/frank-amiga-master.elf
python3 tools/uart.py master send "help\r" --until "^OK"
```

Bench instruments (probes, chip IDs, capture devices) are in `tools/bench.conf`.

## Status

M0 bring-up: clocks, PSRAM, link, HDMI 720x576p50, I2S, microSD verified on hardware.
See `docs/bringup.md`.
