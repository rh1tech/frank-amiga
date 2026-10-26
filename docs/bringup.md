# Bring-up measurements (2026-10-26)

**Clock decision:** both halves run at **504 MHz**, 1.65 V (the clock frank-linux
and murmsnes have proven). 528 MHz was tried first so HSTX could make 720x576p50
from PLL_USB; it produced random single-bit read errors on both chips (corrupted
pointers, return addresses) within ~30 s of real load, while the same code ran
clean at 480 MHz. At 504 MHz clk_hstx = clk_sys/4 = 126 MHz, so the output is
640x480 (a 50 Hz variant with a long vertical blank for PAL, and 60 Hz).

The M0 numbers below were taken at 528 MHz.

## Clocks
- PLL_SYS 1584 MHz VCO / 3 = 528 MHz; clk_usb = PLL_SYS/11 = 48 MHz; clk_peri = PLL_SYS/4 = 132 MHz.
- PLL_USB dedicated to HSTX: 270 MHz (576p50, 480p60) or 252 MHz (640x480); clk_hstx = PLL_USB/2.
- Flash /8 (66 MHz), PSRAM /4 (132 MHz).

## Memory (cycles per random 16-bit access over 512 KB, xorshift addresses)
| | read | write |
|---|---|---|
| PSRAM cached alias (0x11...) | 121 | 226 |
| PSRAM uncached alias (0x15...) | 86 | 55 |
| PSRAM 16 KB working set, cached | ~0 | |
Sequential uncached read: 29.7 MB/s. Same on both halves.
=> Emulated chip RAM in PSRAM goes through the **uncached** alias; the 16 KB XIP
cache is shared with flash code and loses on scattered access.

## Link (PIO, 8-bit each way, 528 MHz)
- Control round trip (128-byte frames, doorbells): 46.6 us.
- Soak, 4 KB blocks alternating directions, CRC-checked words:
  - clkdiv 1.0 (105.6 MB/s wire): 376,302 blocks in 60 s, 0 errors, 32.5 MB/s incl. handshakes
  - clkdiv 1.5: 335,262 blocks in 60 s, 0 errors
  - run with HDMI scanout, I2S and the USB host stack active.

## HDMI
- 720x576p50 / 720x480p60 / 640x480p60 from HSTX, RGB565 rows, each row on two lines.
- HDMI mode (AVI InfoFrame). CEA modes are sent limited-range (16..235): the
  capture card (and many TVs) apply limited range on CEA timings regardless.
- 50 frames/s, 0 late rows. Capture card accepts 720x576@50; geometry pixel-exact.

## Audio
- TDA1387 via PIO I2S at exactly 48 kHz (528 MHz / (171.875 x 64)).
- 1 kHz tone: THD 0.49% (capture at low level). The capture device's sample clock
  wanders, so it is not usable for pitch measurements.

## microSD
- SPI0 at 22 MHz (clk_peri/6): 1.63 MB/s sequential file read.
