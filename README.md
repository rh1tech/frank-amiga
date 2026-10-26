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

## microSD layout

```
/amiga/frank-amiga.ini      kick=/amiga/kick/kick13.rom
                            ext=/amiga/kick/aros-ext.bin   (optional, AROS)
                            df0=/amiga/adf/game.adf        (optional)
/amiga/kick/                Kickstart images (your own licensed copies, or AROS)
/amiga/adf/                 disk images
```

At power-on the master uploads the configured ROMs and disk to the slave and
starts the machine. Console (UART0, 115200): `df0 <path>` changes the disk,
`kick`/`ext <path>` change ROMs and cold-start, `boot` rereads the ini,
`reset` is Ctrl-Amiga-Amiga, `type`/`key`/`mouse` drive input, `stats`.

## Status

- M0 bring-up: clocks (both halves 504 MHz), PSRAM, link, HDMI, I2S, microSD.
- M2/M3: the Omega core runs on the slave; its display/audio message stream
  crosses the link; the master renders Denise and drives HDMI at 640x480@50,
  locked to the emulated PAL frame (FS pacing). Keyboard input from the
  master's console (`type`, `key`, `mouse`). DiagROM boots to its menu.
- M4 (partial): Paula mixed on the master to the I2S DAC, rate-matched.

See `docs/bringup.md`.
