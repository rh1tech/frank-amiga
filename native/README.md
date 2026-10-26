# native/ — head-less desktop runner for the Omega core

Runs the **same `omega/*.c` sources** the RP2350 firmware uses (compiled with
`PICO_BUILD` **undefined**, so the original desktop code paths are taken), linked
against a plain `malloc`'d framebuffer and the 16 MB `low16Meg` array instead of
PSRAM. Displays in a live SDL2 window when SDL2 is available; otherwise dumps
framebuffer snapshots as binary PPM.

Purpose: validate the CPU + chipset + CIA + DMA + Blitter + Floppy logic on a PC
without RP2350 hardware. It does **not** exercise `src/psram.c`, `src/Memory.c`
(PSRAM address translation), `src/main.c`, or the PSRAM framebuffer in
`src/Host.c` — those are RP2350-only.

## Build & run

```bash
./native/build.sh                       # -> native/omega-native
HEADLESS=1 ./native/build.sh            # force PPM-only build, even with SDL2
```

`build.sh` auto-detects SDL2 via `sdl2-config`:
- **SDL2 present** — a live integer-scaled 2× window opens automatically:
  1280×800 for NTSC or 1280×1024 for PAL.
  Press **Esc** or close the window to stop early; the final PPM is still written.
- **SDL2 absent** — headless PPM-only build (install `sdl2` / `libsdl2-dev` for the window).

The ROM is selected via the `OMEGA_ROM` environment variable (256 KB ROMs are
mirrored automatically to fill the 512 KB window at 0xF80000–0xFFFFFF):

```bash
# Kickstart 1.3 — insert-disk screen only (no floppy):
OMEGA_ROM=sd_card/rom/kick13.rom ./native/omega-native "" 500000

# Kickstart 1.3 — boot a WB 1.3 ADF to the AmigaDOS CLI:
OMEGA_ROM=sd_card/rom/kick13.rom ./native/omega-native \
    sd_card/adf/amiga-os-134-workbench.adf 300000 50000 3000
#   ^disk.adf                              ^iters ^dump ^insert-at

# Kickstart 2.04 — ROM disk, no floppy (purple "Insert Workbench" backdrop):
OMEGA_ROM=sd_card/rom/kick204.rom ./native/omega-native "" 500000

# Kickstart 2.04 — boot a WB 2.x / Install ADF:
OMEGA_ROM=sd_card/rom/kick204.rom ./native/omega-native \
    sd_card/adf/Install3.2.adf 500000 4000 3000

# Kickstart 3.14 — ROM-based Workbench, no floppy (boots automatically):
OMEGA_ROM=sd_card/rom/kick314.rom ./native/omega-native "" 500000

# Convert the final PPM snapshot to PNG:
python3 native/ppm2png.py frame_final.ppm frame_final.png
```

For deterministic screenshot checks, force a headless build and capture the
insert-disk screen after 500,000 iterations:

```bash
HEADLESS=1 ./native/build.sh
OMEGA_ROM=sd_card/rom/kick204.rom ./native/omega-native "" 500000 500000
python3 native/ppm2png.py frame_final.ppm frame_final.png
```

The same command works with `sd_card/rom/kick13.rom` and
`sd_card/rom/kick314.rom`. ROMs, ADFs, reference images under `image_refs/`,
and generated `frame*.ppm`/`frame*.png` files are local test assets and must not
be committed.

### KickSmash ROM switcher

The KickSmash32 ROM-bus message protocol is answered by `omega/KickSmash.c`,
the same code as on the board, so the standalone KickSmash ROM-switcher image
lists real banks and its "Switch and Reboot" works. The banks are the `*.rom`
files of the Kickstart's directory (`OMEGA_KS_DIR` overrides it), sorted by
name, up to 8; `kicksmash.cfg` there keeps the switcher's settings (written
when it saves) and may map banks to files. Switching loads the bank's file and
resets the Amiga (Ctrl-Amiga-Amiga style), as the board does.

```sh
VIDEO=PAL ./native/build.sh
OMEGA_ROM="$PWD/sd_card/rom/switcher.rom" ./native/omega-native
```

The KickSmash release 2.0 image needs a 68020 (`OMEGA_CPU=68020`); a build of
release 2.0 with 68000 support (built for the 68000, CPU type checked before
any 68010+ instruction, vectors at address 0 without a VBR, assembly vector
stubs) runs on the default 68000 as well. `KICKSMASH_SIM=1` at build time
echoes the Amiga's serial output (the switcher's log) to stdout.

Native ROM loading accepts images from 1 byte through 512 KB. Images no larger
than 256 KB are zero-padded to one 256 KB bank and mirrored across the complete
Kickstart window, which is required by compact standalone ROMs.

The protocol (`omega/KickSmash.c`): requests arrive as 32-bit ROM reads, a
16-bit word encoded in each address (32- or 16-bit ROM mode), CRC-checked;
the reply is returned by the ROM reads after the trigger read at offset
0x1554 (which returns its first long), as KickSmash's reply DMA drives the bus
whatever the address. Only reads by code running from RAM take part (the
switcher's transport copies itself there; Kickstart reading its own tables
must not). Commands: NOP, ID (reports "OMEGA-RP2350", version 2.0), bank info,
bank set (current/next reset/power-on, with or without reboot), bank names,
long-reset sequence, merge, board name, and the 32 NV bytes (the switcher's
auto-switch timeout and bank); others answer "unknown command". A bank changes
only through a reset: without the reboot option it waits for the next one.
`native/kicksmash_test.c` drives every command as the switcher does
(`native/regression.sh` runs it).

The default native CPU remains a 68000. `OMEGA_CPU=68020` selects Musashi's
68020 core at run time; its full indexed addressing and 68020 CACR behavior are
needed by the release 2.0 switcher image. `OMEGA_CPU=68ec020` selects the
board's 68EC020 (24-bit bus). A KickSmash switch takes the bank's CPU from
`kicksmash.cfg` (`cpu <n> 68020`), as the board does. Coprocessor (FPU, MMU)
instructions take the F-line exception on the 68020 too, as on a real one
without an FPU: Kickstart detects the FPU that way.

### DiagROM

The CPU starts at the ROM's reset vector (offset 4, `omega/CPU.c`), as the
68000 does through the ROM overlay, rather than at the Kickstart `JMP` at
`0xF80002`, where DiagROM keeps its `DG` marker. This applies to the RP2350
build as well:

```sh
VIDEO=PAL ./native/build.sh
OMEGA_ROM="$PWD/sd_card/rom/diagrom.rom" ./native/omega-native
```

DiagROM polls the high byte of `SERDATR` while printing its early diagnostics,
so byte reads expose the same idle `TSRE`/`TBE` state as word reads. Its memory
tests also write through empty Zorro/autoconfig space; with no expansion board
attached these writes are ignored rather than decoded as custom registers.

DiagROM reaches and displays its complete main diagnostic screen, including
the version heading and bottom hardware-status line. DiagROM changes display
mode near the PAL vertical-comparator boundary, so the Copper terminator must
remain stopped after line 255 rather than running into adjacent list data.

### IDE hard disk (A600 Gayle)

`omega/Gayle.c` emulates the A600's Gayle with one IDE drive. Kickstart 2.05
(A600HD 37.350) and 3.1.4 find it with their own `scsi.device` and boot from
it; 1.3 and 2.04 have no IDE driver. `OMEGA_HDF` names the drive's image, a
raw RDB hard-disk image (`hdf_native.c`, opened read-write: boots write to
it). Without `OMEGA_HDF` there is no drive and no Gayle, as before.

`make_hdf.sh` builds a bootable image from a Workbench ADF with amitools
(`rdbtool`, `xdftool`), as an installer would: one FFS partition `DH0`, 63 MB
(`CYLINDERS`, default 130) × 16 heads × 63 sectors, the geometry the drive
reports. For 3.1.4 the Modules disk's libraries go to `Libs`:

```sh
native/make_hdf.sh dh0.hdf System ffs+intl \
    sd_card/adf/amiga-os-314-workbench.adf \
    sd_card/adf/amiga-os-314-modules-a500.adf
OMEGA_HDF="$PWD/dh0.hdf" OMEGA_ROM="$PWD/sd_card/rom/kick314.rom" \
    ./native/omega-native "" 500000

native/make_hdf.sh dh0.hdf System ffs sd_card/adf/amiga-os-205-workbench.adf
OMEGA_HDF="$PWD/dh0.hdf" OMEGA_ROM="$PWD/sd_card/rom/kick205.rom" \
    ./native/omega-native "" 500000
```

Both boot to Workbench from `System` with no floppy (with no disk at all,
3.1.4 stops at its insert-disk screen). What the two drivers do:

- Both check Gayle's ID at 0xDE1000: a write, then eight reads of bit 7,
  0xD0 for an A600. An access at 0xDE109A comes first, so only 0xDE1000 itself
  is the ID register.
- Both probe for the second drive (absent: its registers read 0), IDENTIFY
  the first, and set multiple mode to 16 sectors.
- 3.1.4 addresses sectors by LBA (READ SECTORS, READ MULTIPLE); 2.05 by
  cylinder, head and sector, after INITIALIZE DEVICE PARAMETERS with 16
  heads and 63 sectors, so images must use that geometry.

A boot only reads. To test writes, give the image an `S/User-Startup` that
writes, boot it, and read the results back:

```sh
printf 'Echo >SYS:hello.txt "written by Omega"\nCopy C:Dir TO SYS:DirCopy\n' \
    > User-Startup
xdftool dh0.hdf open part=DH0 + write User-Startup S
OMEGA_HDF="$PWD/dh0.hdf" OMEGA_ROM="$PWD/sd_card/rom/kick314.rom" \
    ./native/omega-native "" 500000
xdftool dh0.hdf open part=DH0 + read hello.txt hello.txt
xdftool dh0.hdf open part=DH0 + unpack check/   # reads every file
```

Both Kickstarts pass this (also with `MakeDir` and `Copy C: ALL`).

### Fast RAM

As on the board, a 2 MB Zorro II Fast RAM board answers autoconfig at
`0xE80000` (`omega/Expansion.c`); Kickstart places it at `0x200000` and the
runner keeps its memory in `low16Meg` at those addresses.
`OMEGA_FAST_RAM_KB=<n>` sets its size (a power of two, 64-8192; 0 for none).

### RTG (Picasso II+)

The runner also has an RTG card: a Picasso II+ (`omega/Picasso2.c`, a
GD5428 with 2 MB of VRAM), configured after the Fast RAM (VRAM at
`0x400000`, registers at `0xE90000`). PrismRTG's PICASSO2 driver drives it;
while the card's monitor switch is on, the window (and the PPM dumps) show
its picture at its own size. `OMEGA_RTG=0` leaves the card out, `OMEGA_RTG=1024`
gives it 1 MB of VRAM as on the board. To try it, put PrismRTG's files on a
3.1.4 image the way `regression.sh` builds `rtg314.hdf` (PrismD, PrismSetup,
`Libs/Prism/PICASSO2.driver`, `Devs/Monitors/Prism`, `PrismSetup WBID=0001`
for PRISM:640x480 8-bit, its `screenmode.prefs` copied to `ENV:` for the
first boot) and boot it with `OMEGA_CPU=68ec020`.

Debugging aids:

* `OMEGA_P2_DEBUG=1` logs every blit of the card (mode, raster op, size,
  addresses, colours), the end of each blit fed by the CPU and engine
  resets during one.
* `OMEGA_MOUSE="frame:dx:dy:buttons,..."` moves and clicks the mouse: from
  each frame the movement (in mouse counts, two per frame, below
  Intuition's acceleration) and the buttons (bit 0 left, 1 right). On the
  RTG screen one count is one pixel; e.g.
  `OMEGA_MOUSE="1700:-800:-800:0,2200:40:40:0,2300:0:0:1,2310:0:0:0"` goes to
  the top-left corner, then onto the first disk icon, and clicks it.
* PrismRTG's own checks build from its sources with the m68k-amigaos-gcc
  toolchain: `PrismTest BOARD=PICASSO2 DEPTH=8 BLIT` (fills and copies
  against the CPU) and `p96test` (the Picasso96 API, 29 checks) pass.

Known PrismRTG 1.2 issue (not the emulator's): a selected icon without its
own selected image is drawn as an empty box. icon.library clears the masked
area with pen 0 and calls BltMaskBitMapRastPort with minterm 0x20 ("invert
the source through the mask"); PrismD's `blit_masked` applies it as a
BltBitMap minterm (`~source AND destination`), which is 0 there. WinUAE shows
the same.

### Screenshot regression suite

`REGRESSION=1` is a native compile-time build mode. It forces a headless build
and removes the large interactive boot-state diagnostics so framebuffer tests
are deterministic and concise:

```sh
REGRESSION=1 VIDEO=PAL ./native/build.sh
```

The regression runner builds in that mode, boots the three ROM-only screens,
the three Workbench/installer cases and Kickstart 3.1.4 and 2.05 from fresh
hard-disk images (`hdf314`, `hdf205`, `hdf314-ec020` on the 68EC020, and
`rtg314`: PrismRTG's Workbench on the Picasso II+), then compares exact final-frame PPM
hashes, sorted by case name:

```sh
VIDEO=PAL  ./native/regression.sh
VIDEO=NTSC ./native/regression.sh
```

It requires the locally supplied ROM and ADF files named in the examples above
(including `kick205.rom` and the 3.1.4 Workbench and A500 Modules and 2.05
Workbench ADFs) and amitools for the hard-disk images.
Each run prints a unique temporary directory containing its PPMs and logs for
manual review. If an intentional rendering change has been reviewed in both
modes, update one baseline set at a time with:

```sh
UPDATE_BASELINES=1 VIDEO=PAL ./native/regression.sh
```

Never update baselines merely to make a failing test pass; inspect the generated
images first. Reference PNGs remain manual visual references because their
canvas sizes and capture sources are not uniform.

Each "iteration" is 200×(`dma_execute()` + `cpu_execute()`), matching the RP2350
`main.c` main loop. Rough timing guide:

| Iterations | Simulated VBLs | Wall time (PC) | Notes |
|---|---|---|---|
| 40 000  |   133 |  ~1 s | Initial screen up (KS 1.3 / 2.04 backdrop) |
| 500 000 | 1 667 |  ~8 s | KS 2.04 + ADF boots to Workbench desktop |
| 500 000 | 1 667 | ~8 s  | KS 3.14 ROM-Workbench fully rendered |

`insert-at` delays the simulated DF0 disk-insert until Kickstart has finished
drive ID mode (`df[0].idMode == 0`); the runner then polls `floppyInsert(0)`
until the drive latches the disk. Use `""` as the disk argument to run without
any floppy image.

The video standard is selected at compile time. NTSC is the default; build PAL
with:

```sh
VIDEO=PAL ./native/build.sh
```

`VIDEO=NTSC` restores the default; lowercase `pal` and `ntsc` are also
accepted. The script is compatible with the Bash 3.2 version shipped by macOS.

The choice is shared by DMA frame length, the chipset video-identification bit,
CIA vertical TOD events, and SDL pacing (59.94 Hz NTSC or 50 Hz PAL), so
VBL-driven animations run in real time. `OMEGA_HEADLESS=1` and binaries built
with `HEADLESS=1` remain unthrottled for fast boot and framebuffer tests.

The native framebuffer follows the selected standard: 640×400 for NTSC and
640×512 for PAL. SDL creates a fixed 2× window and uses nearest-neighbour
integer scaling, so pixels are never resized by a fractional ratio. PPM dumps
use the same selected framebuffer dimensions. The RP2350 framebuffer remains
640×400 because its fixed 8 MB PSRAM map reserves 1 MB for ARGB display output.

PAL uses its own viewport origin and a full-height native intermediate raster.
Video-standard viewport offsets are owned solely by `omega/VideoStandard.h`;
the obsolete duplicate host offset has been removed.
The full beam-row raster is required by full-width LORES copper displays.
Overscan windows such as the Kickstart 1.3 insert-disk requester occupy more
beam rows than the progressive viewport and keep logical picture rows on
alternating raster rows. Normal 200/256-line LORES windows use every raster
row. Presentation derives that choice from the DIW vertical span before
applying the normal 2× integer scale, avoiding both empty scanlines and
truncation of the hand, lower disk artwork, or DiagROM menu. HIRES displays
continue to consume half-height logical rows. The RP2350 keeps its fixed
200-row intermediate raster because of the existing PSRAM layout.

DMA tracks its intermediate destination with separate raster-row and X
coordinates. The planar-to-chunky helpers write relative to the destination
pointer supplied by DMA instead of sharing the old overloaded `FBCounter` for
both line and pixel offsets. Display-window vertical tests also decode the OCS
fixed ninth bits (`VSTART8=0`, `VSTOP8=1`) in one place.

Only an actual bitplane fetch selects the frame's LORES or HIRES presentation
mode. During display blanking, `BPLCON0` contains zero planes; its cleared HIRES
bit must not be interpreted as a LORES screen. Doing so made full-width HIRES
Workbench output sample alternating raster rows and appear at half height.

Legacy framebuffer-position helpers from the former `FBCounter` design have
been removed. `BPLCON0` writes no longer reset a host-side drawing cursor, and
the unused `setDisplayMode()` plus random-pixel `drawBlank()` diagnostic are
gone. Raster placement is now owned solely by the active DMA fetch paths.

Planar-to-chunky conversion APIs take only their destination and bitplane
words. The former palette argument was unused because conversion reads the
live chipset palette, so removing it keeps the interface consistent with the
actual data dependency.

HIRES, LORES, and HAM planar conversion now live in the shared
`src/Planar.c`. Native and RP2350 builds link the same implementation instead
of carrying byte-for-byte copies in their host backends.

Framebuffer presentation now lives in shared `src/Presentation.c`. Clipping,
LORES row selection, scanline expansion, wrapped-prefix repair, wrapped-row
centering, and raster clearing therefore have one implementation for native
and RP2350 builds. Each host backend retains only platform-specific frame
submission and lifecycle work.

The shared presentation code names its display-layout signatures, row
rotation, wrapped-prefix width, and OCS vertical-bank size rather than
scattering literal register and pixel values through the algorithm. Repeated
full-width and wrapped-window predicates are expressed as helpers, including
removal of a redundant wrapped-origin test.

DDF layout signatures shared by DMA and presentation are defined in
`omega/DisplayLayout.h`. Full-width fetch detection, the extra-word requester
layout, and the rotated Workbench 3.14 layout can no longer drift between the
two stages. Full-width presentation now follows DDF geometry alone; the former
additional dependency on an early DIW vertical start was redundant and has
been removed. Presentation calls the shared DDF predicate directly instead of
wrapping it in another local screen-shape helper.

The same layout header also names the normal and extra-word HIRES fetch tails,
the LORES fetch span, and the two upper-overscan offsets. DMA no longer embeds
those timing and placement values directly in its control flow. Full-width
row rotation is expressed as two contiguous segments rather than a per-pixel
modulo loop.

Bitplane DMA now has an explicit per-scanline state object for the terminal
fetch cycle and HIRES/LORES word counts. This replaces unrelated loose globals
and provides one owner for the fetch-completion and modulo timing model. The
current modulo behavior is intentionally unchanged at this foundation step.

Scanline reset and bitplane-pointer advancement are now explicit operations on
that boundary, and the state records whether a real plane fetch occurred. For
compatibility, modulo is still applied at the existing host scanline boundary;
the fetch marker allows no-fetch advancement to be measured before replacing
it with hardware DDF-completion timing.

Fetch tracking covers every enabled plane, not only plane 1. This captures
partial Copper transition lines where higher planes fetch before `BPLCON0`
blanks the display. Fully blank lines still use compatibility modulo
advancement because removing it currently exposes incomplete Copper pointer
reload behavior in the 2.04 and 3.14 layouts.

The scanline state stores a per-plane fetch mask rather than a single boolean.
Partial Copper transition lines can therefore distinguish odd-plane and
even-plane activity, which is necessary before their two modulo registers can
be timed independently. It also retains the union of planes enabled at any
point during the line, separately from planes that actually fetched, so
Copper mode changes are not reduced to the final `BPLCON0` value. A per-plane
fetch-eligible mask is accumulated while bitplane DMA and the display window
are both active, distinct from both merely enabled planes and planes for which
a DMA word was observed. Real fetch marking is shared by all implemented OCS
plane paths. Obsolete per-plane/group cycle diagnostics and the redundant
completed-fetch copy were removed once DDF completion could consume the live
fetch mask directly. On an otherwise eligible line
where no fetch occurs, fallback advancement is limited to that eligible set;
fully ineligible lines reuse the most recently fetched plane set. The legacy
all-plane startup fallback has been removed, so no pointer receives speculative
modulo advancement before the first observed bitplane fetch.

The `BPLCON0` decoder caps its plane count to the implemented OCS DMA
capacity—four planes in HIRES and six in LORES—so every consumer sees one
supported mask. Fetch eligibility uses that decoded mask directly instead of
independently interpreting raw register bits.

Zero-plane `BPLCON0` no longer requires an early return in `bitplaneActive()`.
The former Workbench-specific guard became redundant after display mode state
was separated from blanking and has been removed without changing output.

The final DDF cycle explicitly snapshots the completed per-plane fetch mask.
This provides a stable completion hook for relocating modulo timing later,
without yet changing the scanline-boundary application used by rendering. The
corresponding HIRES pointers for planes 1–4 are captured at the same boundary,
separating the fetched line from any later Copper pointer writes. The
narrow-DDF right-edge prefetch now reads these captured pointers instead of
live end-of-line values, preventing late Copper writes from changing a line
whose fetch window already completed. Modulo for planes that really fetched is
now applied exclusively at this DDF-completion boundary. The former
end-of-line safety application and its double-advance mask have been removed;
no-fetch compatibility advancement is the only modulo operation that remains
at the scanline boundary.

Narrow-window prefix reconstruction remains necessary for wrapped DIW/DDF
layouts: removing it visibly moves the 3.1.4 requester’s right-edge graphics
to the left side of the following row. The condition is based on display
geometry rather than Kickstart identity; code comments use the same generic
terminology. The current selector is nevertheless indirect: a vertical
256-line DIW-bank crossing enables horizontal fetch-row reconstruction. This
coupling is named explicitly. Raw raster-row width was tested as an alternative
selector, but both 2.04 and 3.1.4 requesters produce 145 rows of 624 pixels, so
the insufficient diagnostic state was removed. DMA and presentation now share
the OCS ninth-bit decoding of DIW vertical start and stop values. Vertical-bank
crossing detection also lives in that shared layout layer; presentation no
longer interprets raw DIW values to select requester reconstruction.
The remaining content-aware work is contained in named helpers for locating a
row's rightmost visible pixel and measuring the following row's wrapped prefix,
instead of being embedded in the reconstruction loop.
Prefix extent is measured through the last non-border pixel in the complete
prefix window. A wrapped row may legitimately contain the border colour inside
its artwork; treating the first such pixel as the endpoint truncated later
segments and produced a purple line through the animated 3.1.4 floppy.

Bitplane pointer register writes are tracked per plane and per scanline. Both
CPU and Copper writes pass through the same register handlers, allowing DMA to
correlate pointer reloads with the planes that fetched before blank-line
compatibility advancement is removed. High- and low-word writes are recorded
separately, so completeness is derived directly from their intersection rather
than stored in a redundant third mask or transition state. Repeated complete
writes are idempotent. Only completions before DDF affect the
blank-line policy, so they are retained in one bitmask rather than a per-plane
phase array; unused during/after classifications were removed. Cross-scanline
reload diagnostics were also tested and removed after they proved the fallback
dependency was unrelated to pending complete reloads.

Partial scanlines now apply `BPL1MOD` and `BPL2MOD` only to odd/even plane
members that actually fetched, so an unfetched sibling pointer no longer
advances merely because another plane in its modulo group was active. Fully
blank lines retain compatibility
advancement at the scanline boundary, except when every active plane in a
group completed a pointer reload before DDF. Requiring the full active group
avoids treating a partial Copper pointer update as a reload of all odd or even
planes. The active set is accumulated across the full scanline rather than
taken from the final `BPLCON0` value, preserving modes that the Copper changes
before the line ends. Because pointer advancement is now per plane, a
pre-fetch reload suppresses fallback advancement only for that active plane;
unrelated pointers in the same modulo group are unaffected. Applying modulo
at the emulated terminal fetch slot was tested and rejected for now because it
shifted the extra right-edge fetch used by the Kickstart 2.04 requester.
Modulo policy selection and pointer mutation are now separate internal steps,
so a future timing change can relocate application without duplicating the
per-plane and compatibility rules.

The shared layout definitions also name the raster origin at beam line 43 and
the first LORES render line at 44. LORES and HIRES positioning now refer to
those meanings rather than repeating unexplained vertical literals.
Their scanline destinations are initialized at every plane-1 DMA slot even
when plane 1 is disabled. LORES also clears its plane-1 data latch before an
optional fetch, matching HIRES, while full-width negative upper-overscan lines
reject every word. Together these prevent transition frames from combining
fresh higher planes with stale plane-1 data or raster coordinates.

The unused `sprite2chunky()` API and its duplicate host implementations have
been removed. This does not remove working sprite support: `spriteCycle()` is
currently an explicit stub, so sprite DMA and sprite rendering remain
unimplemented. Implementing the sprite fetch/state machine is still required
before a shared sprite converter should be introduced again.

Always-on rendering investigation logs have also been removed from the DMA and
custom-register hot paths. Per-line HIRES, `BPLCON0`, and modulo writes no
longer add console I/O or timing noise during normal emulation; screenshot
regression logs retain the runner-level information needed for failures.

The unused host-side `vblCount` member and its native/RP2350 initializations
have been removed. Native frame counting remains in the native runner's
separate `native_frame_counter` state.

## Status (2026-09-08)

The rendering/DMA cleanup checklist is complete. Remaining compatibility
behavior is documented where it is implemented: content-aware wrapped-prefix
reconstruction and no-fetch bitplane modulo advancement. Sprite DMA and
rendering are implemented in `omega/DMA.c` (see the top-level README);
sprite collisions (CLXDAT) are not.

- Kickstart 1.3 boots to the complete "insert Workbench" screen. ✅
  The native host records the full PAL/NTSC beam field and tracks whether the
  active display is LORES or HIRES. Full-width LORES output is deinterlaced
  before 2× presentation, so the requester has continuous scanlines, correct
  vertical scale, and its complete lower half.
- Loaded Workbench screens retain their full vertical height. ✅
  Zero-plane blanking no longer changes the remembered display resolution, so
  HIRES frames are not mistakenly passed through the LORES alternating-row
  presentation path. This applies to Workbench 1.3, 2.04, and 3.14 while
  leaving their insert-disk requester output unchanged.
- `original2.adf` (WB 1.3.2 UK) boots through the startup-sequence to `[CLI 2]`. ✅
  Full-width HIRES Workbench screens use a true 640-pixel raster stride rather
  than the narrower wrapped-fetch presentation used by the Kickstart artwork.
  PAL Copper waits that cross line 255 retain the next vertical-line bank, and
  the `FFFF FFFE` list terminator is frozen explicitly. Treating that marker as
  a normal 16-bit WAIT allowed PAL line 256 to release it and execute adjacent
  data as Copper instructions. The 40-line upper overscan is removed before
  presentation. This keeps complete Workbench and DiagROM screens—including
  their lower borders and status lines—in the output.
- Kickstart 2.04 boots Workbench 2.x from ADF (`Install3.2.adf` confirmed). ✅
  Its `DDFSTRT=0x3c`, `DDFSTOP=0xd4` HIRES window consumes 40 words per
  bitplane row; the extra fetch needed by the narrower 1.3 window is applied
  only to `0x3c–0xd0`. This prevents the row-by-row pointer drift that produced
  diagonally offset blocks. The desktop is coherent, although its final PAL
  viewport framing still needs refinement.
  Byte reads outside the 32 readable custom-register byte handlers return open
  bus instead of indexing beyond the dispatch table; this is shared by the
  native and RP2350 memory backends.
- Kickstart 2.04 no-disk boot: insert-disk screen renders correctly. ✅
  Rainbow V-checkmark, both floppy-disk icons, and all four copyright-text lines
  are visible. Fixed by: (a) floppy drive ID/motor-on reports `/DKRDY=0` for
  drive 0 regardless of ADF; (b) no-disk MFM tracks pre-filled with sync word
  so `DSKBLK` fires and trackdisk returns `TDERR_NoSecHdr`; (c) HIRES bitplane
  fetch count corrected to the real OCS formula (`lastFetchCycle = ddfstop + 7`,
  giving `(ddfstop − ddfstrt)/4 + 2` words/line); (d) `BPL1MOD` sign-extended
  correctly (`(int16_t)value >> 1`).
  The native/RP2350 host presentation step now separates the raw DMA raster
  from the displayed framebuffer, clips fetch-pipeline overscan, and doubles
  visible scanlines. Narrow HIRES screens retain their native horizontal pixels
  rather than passing through the former lossy 27:32 reduction, which had
  compressed complete requester artwork and discarded columns from text.
Vertically wrapped fetch rows discard their raw 40-pixel left origin after
prefix reconstruction, keeping the complete logical row centred on-screen.
Prefix clearing and centering are performed together per logical scanline,
avoiding a redundant traversal of the complete framebuffer. The normal narrow
raster span is contiguous and is copied as one bounded row segment rather than
through a per-pixel loop.
Presentation paths clear their intermediate raster explicitly through a shared
fill helper; the former label-and-`goto` cleanup exit is no longer needed. The
same helper fills the translated wrapped row's trailing border, leaving that
operation entirely segment-based. All presentation paths also use one shared
  scanline-duplication helper for their 2× vertical output.
  This presentation is shared by Kickstart 1.3, 2.04, and 3.14.
  The former top-line square flash during the 2.04 disk animation was caused by
  a stale LORES plane-1 latch and destination row; both are now refreshed at
  the DMA slot independently of the enabled-plane mask.
- Kickstart 3.14 boots ROM-based Workbench (grey backdrop + title bar). ✅
  (Regression after CIA ICR fix was resolved by implementing the chipset
  slow-RAM mirror: reads at `0xCxxxxxx & 0xFFF < 0x20` now route to the
  correct chipset read-register instead of raw chip RAM.) Its full-width
  `DDFSTRT=0x38`, `DDFSTOP=0xd8` desktop raster requires an 80-pixel circular
  presentation correction; this keeps the rightmost segment on the right
  instead of wrapping it to the left edge. The correction is shared by the
  native and RP2350 hosts and does not affect the 1.3 or 2.04 desktop modes.
  DMA associates those leading pipeline words with the preceding logical row,
  so the rotated right edge no longer has a one-scanline seam or requires
  presentation to join neighboring rows.
- Battery-clock reads at `0xDC0000` return junk (`<invalid>` from `date`) — the
  Gayle/RTC path is a stub; unrelated to the RP2350 port.
- The ROM diskette-logo bitmap renders horizontally mirrored on the insert
  screen; normal Workbench text/graphics render correctly. Same Blitter/DMA code
  on both targets — cosmetic, low priority.

## Files

| File | Role |
|---|---|
| `main_native.c`   | entry: load ROM/ADF, run loop, dump PPM |
| `host_native.c`   | native framebuffer lifecycle, PPM inspection, and SDL frame submission |
| `../src/Planar.c` | shared HIRES, LORES, and HAM planar-to-chunky conversion |
| `../src/Presentation.c` | shared raster clipping, repair, scaling, and framebuffer presentation |
| `memory_native.c` | upstream Omega `Memory.c` verbatim (`low16Meg`, `chipRead*/Write*`) |
| `hdf_native.c`    | the IDE drive's image (`OMEGA_HDF`) for `omega/Gayle.c` |
| `make_hdf.sh`     | bootable RDB image from a Workbench ADF (amitools) |
| `display_sdl.c`   | SDL2 window: open / push (30 fps cap) / poll / close |
| `display_sdl.h`   | public API for `display_sdl.c` |
| `sdl_shim.h`      | no-op `SDL_AtomicGet/Set` so `waitFreeSlot()` links without SDL |
| `build.sh`        | gcc build; auto-detects SDL2, compiles `display_sdl.c` without the shim |
| `ppm2png.py`      | dependency-free P6-PPM → PNG |

`main_native.c` normally steps one DMA slot and then the CPU. Building with
`-DOMEGA_NATIVE_SLICE=<n>` runs them in `n`-slot batches like the RP2350 main
loop, which exercises multi-slot `dma_run()` paths; compare against a build
with `-DOMEGA_NO_SLOT_SKIP` to check that skipping idle slots changes nothing.
The regression baselines use the default per-slot loop.

