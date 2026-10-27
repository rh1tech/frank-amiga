// Head-less native runner for the Omega Amiga core.
//
// Builds the SAME omega/*.c sources the RP2350 firmware uses (with PICO_BUILD
// undefined, so the desktop code paths are taken), links them against a plain
// malloc'd framebuffer and the 16 MB low16Meg array, boots a Kickstart ROM
// plus an ADF, runs the emulator for a while and dumps framebuffer PPMs.
//
// Usage: omega-native [disk.adf] [iterations] [dump_every] [insert_at]
//   disk.adf     ADF image for DF0:              (default: none)
//   iterations   number of 200x(dma+cpu) batches (default: 40000)
//   dump_every   write frameNNNN.ppm every N batches (default: 4000)
//   insert_at    batch index at which to "insert" DF0: (default: 3000;
//                Kickstart must finish drive identification first)
//
// Environment:
//   OMEGA_ROM=<file>   load this Kickstart ROM (images up to 256 KB are
//                      zero-padded and mirrored; larger images fill 512 KB)
//                      instead of the built-in Kickstart 1.3.
//   OMEGA_DISASM=1     turn on the Musashi disassembler (to UART/stdout) -
//                      useful for seeing where a ROM's early init diverges.
//   OMEGA_CPU=68020     use Musashi's 68020 core instead of the default 68000;
//                       68ec020 for the board's 24-bit 68EC020. A KickSmash
//                       switch takes the bank's CPU ("cpu <n> 68020" in
//                       kicksmash.cfg).
//   OMEGA_FAST_RAM_KB=<n>  Zorro II Fast RAM board size (default 2048,
//                       as the board; 0 for none; omega/Expansion.c)
//   OMEGA_P2_DEBUG=1    log the RTG card's blits
//   OMEGA_RTG=0|1024    no Picasso II+ RTG card (omega/Picasso2.c), or
//                       one with 1 MB of VRAM (as the board) instead of 2;
//                       the window shows its picture while its switch is on
//   OMEGA_HDF=<file>    attach this RDB hard-disk image as the A600 IDE
//                       drive (omega/Gayle.c; see make_hdf.sh)
//   OMEGA_MOUSE=<list>  move and click: "frame:dx:dy:buttons,..." (counts,
//                       spread two a frame; buttons bit 0 left, 1 right)
//   OMEGA_KEYS=<list>   type Amiga raw keys: "frame:code,..." (hex code),
//                       e.g. "200:04,260:02" presses and releases key 4 at
//                       frame 200, then key 2 at frame 260.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "Memory.h"          // src/Memory.h -> #else branch: extern low16Meg
#include "Host.h"

#ifdef HAVE_SDL2
#include "display_sdl.h"
#endif
#include "../omega/CPU.h"
#include "../omega/Expansion.h"
#include "../omega/Picasso2.h"
#include "../omega/Chipset.h"
#include "../omega/CIA.h"
#include "../omega/DMA.h"
#include "../omega/Floppy.h"
#include "../omega/VideoStandard.h"
#include "../omega/m68k.h"
#include "../omega/KickSmash.h"
#include "kicksmash_native.h"

/* frank-amiga: no built-in Kickstart (Kick13.h removed). */

extern unsigned long native_frame_counter;
extern unsigned long native_nonblack_pixels(void);
extern void          native_dump_ppm(const char *path);
extern void          memoryNativeInit(void);

static uint32_t native_read_u32(uint32_t address) {
    return ((uint32_t)low16Meg[address] << 24) |
           ((uint32_t)low16Meg[address + 1] << 16) |
           ((uint32_t)low16Meg[address + 2] << 8) |
           low16Meg[address + 3];
}

int screenWidth  = 640;   // referenced by some omega translation units
int screenHeight = 200;
int disass       = 0;     // Musashi disassembler (toggled by OMEGA_DISASM)

// Load a Kickstart ROM file into low16Meg at 0xF80000. Images no larger than
// 256 KB occupy a zero-padded 256 KB bank which is mirrored at 0xFC0000.
// Images between 256 KB and 512 KB occupy a zero-padded 512 KB window.
// Returns 0 on success.
static int load_rom_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) { printf("  ROM: cannot open %s\n", path); return 1; }
    off_t sz = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (sz <= 0 || sz > 0x80000) {
        printf("  ROM: %s is %lld bytes (expected 1 to 524288)\n",
               path, (long long)sz);
        close(fd);
        return 1;
    }

    memset(&low16Meg[0xF80000], 0, 0x80000);
    size_t loaded = 0;
    while (loaded < (size_t)sz) {
        ssize_t got = read(fd, &low16Meg[0xF80000 + loaded],
                           (size_t)sz - loaded);
        if (got <= 0) {
            printf("  ROM: failed to read %s\n", path);
            close(fd);
            return 1;
        }
        loaded += (size_t)got;
    }
    if (sz <= 0x40000) {
        memcpy(&low16Meg[0xFC0000], &low16Meg[0xF80000], 0x40000);
    }
    close(fd);
    printf("  ROM: %s loaded (%lld KB)\n", path, (long long)(sz >> 10));
    return 0;
}

// frank-amiga: OMEGA_REGW_LOG=<frame> logs bitplane pointer writes then.
void frankRegLog(const char *name, uint16_t value) {
    extern unsigned long native_frame_counter;
    static long at = -2, to;
    if (at == -2) {
        const char *e = getenv("OMEGA_REGW_LOG");
        at = -1;
        if (e && sscanf(e, "%ld:%ld", &at, &to) == 1) to = at;
    }
    if (at < 0 || (long)native_frame_counter < at || (long)native_frame_counter > to) return;
    printf("REGW f%lu v%d h%d %s = %04x (cpu pc %06x)\n", native_frame_counter, internal.vPos,
           internal.hPos, name, value, m68k_get_reg(NULL, M68K_REG_PPC) & 0xffffff);
}
#ifdef FRANK_TRACE_EXCEPTIONS
extern unsigned long native_frame_counter;
void frankTraceException(unsigned vector, unsigned pc) {
    if (vector >= 32 && vector < 48) return;   /* TRAP #n: routine */
    printf("  vbl=%lu EXCEPTION vector %u at pc %06x\n", native_frame_counter, vector, pc & 0xffffff);
    fflush(stdout);
}
#endif
int main(int argc, char **argv) {
    const char  *adfPath   = (argc > 1) ? argv[1] : NULL;
    long         iterations = (argc > 2) ? strtol(argv[2], NULL, 0) : 40000;
    long         dumpEvery  = (argc > 3) ? strtol(argv[3], NULL, 0) : 4000;
    long         insertAt   = (argc > 4) ? strtol(argv[4], NULL, 0) : 3000;
    int          haveImage  = 0;

    const char *romPath = getenv("OMEGA_ROM");
    if (getenv("OMEGA_DISASM")) disass = 1;

    printf("Omega native runner\n");
    printf("  video=%s  lines=%d  refresh=%.3f Hz\n",
           OMEGA_VIDEO_NAME, OMEGA_VIDEO_FRAME_LINES,
           (double)OMEGA_VIDEO_RATE_NUMERATOR /
           OMEGA_VIDEO_RATE_DENOMINATOR);
    printf("  iterations=%ld  dump_every=%ld  disasm=%d\n",
           iterations, dumpEvery, disass);

    // ── Kickstart ROM into low16Meg at 0xF80000 ─────────────────────────
    if (romPath) {
        if (load_rom_file(romPath)) return 1;
        kicksmash_native_init(romPath);
    } else {
        // frank-amiga: no built-in Kickstart (the upstream Kick13.h is a
        // copyrighted ROM image and is not part of this tree).
        printf("  ROM: set OMEGA_ROM=<kickstart image>\n");
        return 1;
    }
    const char *extPath = getenv("OMEGA_EXT_ROM");
    if (extPath) {
        extern int extRomPresent;
        FILE *f = fopen(extPath, "rb");
        if (!f || fread(&low16Meg[0xE00000], 1, 0x80000, f) != 0x80000) {
            printf("  EXT ROM: cannot read %s\n", extPath);
            return 1;
        }
        fclose(f);
        extRomPresent = 1;
        printf("  EXT ROM: %s at 0xE00000\n", extPath);
    }
    printf("  ROM: reset vector %02x %02x %02x %02x, entry %08x\n",
           low16Meg[0xF80000], low16Meg[0xF80001],
           low16Meg[0xF80002], low16Meg[0xF80003],
           (low16Meg[0xF80004] << 24) | (low16Meg[0xF80005] << 16) |
           (low16Meg[0xF80006] << 8)  |  low16Meg[0xF80007]);
    if (low16Meg[0xF80000] != 0x11) {
        printf("  ROM: bad signature (expected 0x11)\n");
        return 1;
    }

    // ── DF0: floppy (optional) ──────────────────────────────────────────
    // Always call floppyInit(0) so the drive is properly identified (idMode=-1
    // triggers the 32-pulse ID sequence that Kickstart needs to see before it
    // will attempt disk I/O).  Without this the ROM skips to the ROM disk.
    if (adfPath && *adfPath) {
        int fd = open(adfPath, O_RDONLY);
        if (fd < 1) {
            printf("  DF0: cannot open %s — drive present, no disk\n", adfPath);
            floppyInit(0);
        } else {
            ADF2MFM(fd, floppyInit(0));
            close(fd);
            haveImage = 1;
            if (insertAt <= 0) {
                // A non-positive insert time models media present at reset.
                df[0].hasDisk = 1;
                df[0].pra &= 0xFB;   // /CHNG=0 (media changed)
                df[0].pra &= 0xDF;   // /DKRDY=0 (media ready)
                printf("  DF0: %s encoded to MFM — disk in drive at boot\n",
                       adfPath);
            } else {
                // Keep the encoded image available, but expose an empty drive
                // until the emulation loop calls floppyInsert().
                printf("  DF0: %s encoded to MFM — insert scheduled at %ld\n",
                       adfPath, insertAt);
            }
        }
    } else {
        floppyInit(0);
        printf("  DF0: drive present, no disk\n");
    }

    // When drive 0 has no disk, pre-fill every track with a sync word (0x4489)
    // at byte 0.  Without this, diskCycle reads all-zero bytes forever, DSKBLK
    // never fires, trackdisk.device never returns an error, and the "Please
    // insert a Workbench disk" requester never opens.  The zero data after each
    // sync produces invalid sector headers so trackdisk returns TDERR_NoSecHdr
    // after its retry limit and the OS shows the insert-disk screen.
    if (!haveImage) {
        uint8_t *mfm = df[0].mfmData;
        for (int cyl = 0; cyl < 82; cyl++) {
            for (int side = 0; side < 2; side++) {
                int base = cyl * (12798 * 2) + side * 12798;
                mfm[base + 0] = 0x44;
                mfm[base + 1] = 0x89;
            }
        }
    }

    // Initialize empty drives 1-3 so pra starts with correct /CHNG=1 and
    // /DKRDY=1 (stable, not ready, no disk) instead of pra=0.
    floppyInit(1);
    floppyInit(2);
    floppyInit(3);

    // ── Bring up the emulator ───────────────────────────────────────────
    hostInit();
    // OMEGA_FAST_RAM_KB: the Zorro II Fast RAM board (default 2048, as the
    // board; 0 for none).
    if (getenv("OMEGA_A500")) {   // frank-amiga: 512 KB chip + 512 KB slow
        extern uint32_t nativeChipMask, nativeSlowSize;
        nativeChipMask = 0x7FFFFu;
        nativeSlowSize = 0x80000u;
    }
    const char *fastKb = getenv("OMEGA_FAST_RAM_KB");
    expansionFastRamSize = (fastKb ? (uint32_t)atoi(fastKb) : 2048u) << 10;
    // OMEGA_RTG: the Picasso II+ RTG card (omega/Picasso2.c): 0 for none,
    // 1024 for 1 MB of VRAM (as the board has), else 2 MB.
    const char *rtg = getenv("OMEGA_RTG");
    expansionPicasso2Vram = !rtg ? P2_VRAM_SIZE
                          : atoi(rtg) == 0 ? 0
                          : atoi(rtg) == 1024 ? P2_VRAM_SIZE / 2 : P2_VRAM_SIZE;
    // OMEGA_P2_DEBUG=1: log the RTG card's blits (and the end of each
    // blit fed by the CPU, and engine resets during one).
    extern int p2DebugBlits;
    p2DebugBlits = getenv("OMEGA_P2_DEBUG") ? atoi(getenv("OMEGA_P2_DEBUG")) : 0;
    memoryNativeInit();
    cpu_init();
    const char *cpuType = getenv("OMEGA_CPU");
    if (cpuType && strcmp(cpuType, "68020") == 0) {
        m68k_set_cpu_type(M68K_CPU_TYPE_68020);
        printf("  CPU: 68020\n");
    } else if (cpuType && strcmp(cpuType, "68ec020") == 0) {
        cpu_select_68020(1);
    }
    ChipsetInit();

    printf("Entering emulation loop\n");
    fflush(stdout);

#ifdef HAVE_SDL2
    if (!getenv("OMEGA_HEADLESS"))
        sdl_display_open(SCREEN_W, SCREEN_H);
#endif

    int dumpIndex = 0;
    for (long it = 0; it < iterations; ++it) {
#ifdef HAVE_SDL2
        if (sdl_display_poll()) break;
#endif
#ifdef OMEGA_NATIVE_SLICE
        // Test aid: run DMA and CPU in slices of OMEGA_NATIVE_SLICE slots, as
        // the RP2350 main loop does, to exercise multi-slot dma_run() paths
        // (e.g. A/B against -DOMEGA_NO_SLOT_SKIP). Not used by regression.sh.
        for (int i = 0; i < 200; i += OMEGA_NATIVE_SLICE) {
            dma_run(OMEGA_NATIVE_SLICE);
            m68k_execute(16 * OMEGA_NATIVE_SLICE);
        }
#else
        for (int i = 0; i < 200; ++i) {
            dma_execute();
            cpu_execute();
        }
#endif

#ifdef FRANK_TRACE_EXCEPTIONS
        {   // Kickstart 1.3's dead-end Alert code (LED flashing): report once.
            static int alerted;
            const unsigned pc = m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff;
            static unsigned lastRamPc; static int left;
            if (native_frame_counter > 20000 && !left) {
                if (pc < 0xf80000) lastRamPc = pc;
                else if (lastRamPc) {
                    left = 1;
                    printf("  vbl=%lu LEFT RAM: last ram pc %06x -> rom pc %06x D7=%08x A7=%08x SR=%04x\n",
                           native_frame_counter, lastRamPc, pc, m68k_get_reg(NULL, M68K_REG_D7),
                           m68k_get_reg(NULL, M68K_REG_SP), m68k_get_reg(NULL, M68K_REG_SR));
                    const unsigned sp = m68k_get_reg(NULL, M68K_REG_SP);
                    for (int k = 0; k < 12; k++) printf("%08x ", native_read_u32(sp + 4 * k));
                    printf("\n");
                }
            }
            if (!alerted && pc >= 0xfc3000 && pc < 0xfc3100) {
                alerted = 1;
                printf("  vbl=%lu ALERT pc %06x D7=%08x D0=%08x A5=%08x A6=%08x SP=%08x\n",
                       native_frame_counter, pc, m68k_get_reg(NULL, M68K_REG_D7),
                       m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_A5),
                       m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_SP));
                const unsigned sp = m68k_get_reg(NULL, M68K_REG_SP);
                for (int k = 0; k < 16; k++) printf("%08x ", native_read_u32(sp + 4 * k));
                printf("\n");
            }
        }
#endif
        {   // frank-amiga: OMEGA_DUMP_AT=<frame> writes chip RAM to chip.bin then.
            static long dat = -2;
            if (dat == -2) { const char *e = getenv("OMEGA_DUMP_AT"); dat = e ? atol(e) : -1; }
            if (dat >= 0 && (long)native_frame_counter == dat) {
                FILE *f = fopen("chip.bin", "wb");
                if (f) { fwrite(low16Meg, 1, 0x80000, f); fclose(f); }
                printf("DUMP f%lu chip.bin\n", native_frame_counter);
                dat = -1;
            }
        }
        {   // frank-amiga: OMEGA_REGS_AT=<frame> prints the display registers then.
            static long at = -2;
            if (at == -2) { const char *e = getenv("OMEGA_REGS_AT"); at = e ? atol(e) : -1; }
            if (at >= 0 && (long)native_frame_counter == at && internal.vPos == 100) {
                printf("REGS f%lu bplcon0 %04x con1 %04x con2 %04x ddf %04x-%04x diw %04x-%04x mod %d %d"
                       " bpl %06x %06x %06x %06x %06x %06x cop1lc %06x dmacon %04x\n",
                       native_frame_counter, chipset.bplcon0, chipset.bplcon1, chipset.bplcon2,
                       chipset.ddfstrt, chipset.ddfstop, chipset.diwstrt, chipset.diwstop,
                       chipset.bpl1mod, chipset.bpl2mod, chipset.bpl1pt, chipset.bpl2pt, chipset.bpl3pt,
                       chipset.bpl4pt, chipset.bpl5pt, chipset.bpl6pt, chipset.cop1lc, chipset.dmaconr);
                printf("CPU f%lu pc %06x sr %04x d0 %08x d1 %08x a0 %08x a7 %08x intena %04x intreq %04x"
                       " dsklen %04x dskpt %06x adkcon %04x ciaa.pra %02x ciab.prb %02x\n",
                       native_frame_counter, m68k_get_reg(NULL, M68K_REG_PC) & 0xffffff,
                       m68k_get_reg(NULL, M68K_REG_SR), m68k_get_reg(NULL, M68K_REG_D0),
                       m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_A0),
                       m68k_get_reg(NULL, M68K_REG_SP), chipset.intenar, chipset.intreqr,
                       chipset.dsklen, chipset.dskpt, chipset.adkconr, CIAA.pra, CIAB.prb);
                {   // the Copper list it runs, as MOVE/WAIT words
                    uint32_t pc = (chipset.cop1lc << 1) & 0x7FFFEu;
                    extern uint32_t nativeChipMask;
                    for (int k = 0; k < 6000; k++, pc += 4) {
                        const uint8_t *m = &low16Meg[pc & nativeChipMask];
                        printf("COP %06x %02x%02x %02x%02x\n", pc, m[0], m[1], m[2], m[3]);
                        if (m[0] == 0xff && m[1] == 0xff && m[2] == 0xff && m[3] == 0xfe) break;
                    }
                }
                at = -1;
            }
        }
        // KickSmash "Switch and Reboot": the bank's ROM, then a reset, as
        // the board does (between slices, once the reply was read).
        {
            const int bank = kickSmashTakeSwitch();
            char bankPath[600];
            if (bank >= 0 &&
                kicksmash_native_bank_path(bank, bankPath, sizeof bankPath) &&
                !load_rom_file(bankPath)) {
                printf("  vbl=%lu KickSmash: bank %d\n",
                       native_frame_counter, bank);
                kicksmash_native_switched(bank);
                cpu_keyboard_reset();
            }
        }

        // OMEGA_KEYS: press each listed key at its frame, release it the
        // next frame (as the keyboard's serial line would deliver it).
        static const char *keys;
        static int keysRead, keyReleaseFrame = -1;
        static uint8_t keyHeld;
        if (!keysRead) {
            keysRead = 1;
            keys = getenv("OMEGA_KEYS");
        }
        if (keyReleaseFrame >= 0 &&
            native_frame_counter >= (unsigned long)keyReleaseFrame) {
            CIAWrite(&CIAA, 0xC, (uint8_t)~((keyHeld << 1) | 1));
            keyboardInt();
            keyReleaseFrame = -1;
        } else if (keyReleaseFrame < 0 && keys && *keys) {
            char *next;
            const unsigned long frame = strtoul(keys, &next, 10);
            if (native_frame_counter >= frame && *next == ':') {
                keyHeld = (uint8_t)strtoul(next + 1, &next, 16);
                keys = *next == ',' ? next + 1 : next;
                CIAWrite(&CIAA, 0xC, (uint8_t)~(keyHeld << 1));
                keyboardInt();
                keyReleaseFrame = (int)native_frame_counter + 1;
                printf("  vbl=%lu key %02x\n", native_frame_counter, keyHeld);
            }
        }

        // OMEGA_MOUSE: "frame:dx:dy:buttons,..." - from each frame, move
        // the mouse by dx/dy counts (two a frame, below Intuition's
        // acceleration) and hold the buttons (bit 0 left, 1 right), as
        // the board's USB mouse does (src/usb_input.c).
        static const char *mouse;
        static int mouseRead, mouseDx, mouseDy;
        static unsigned long mouseFrame;
        if (!mouseRead) {
            mouseRead = 1;
            mouse = getenv("OMEGA_MOUSE");
        }
        if (mouse && native_frame_counter != mouseFrame) {
            mouseFrame = native_frame_counter;
            char *next;
            const unsigned long frame = strtoul(mouse, &next, 10);
            if (*mouse && native_frame_counter >= frame && *next == ':') {
                mouseDx += (int)strtol(next + 1, &next, 10);
                mouseDy += (int)strtol(next + 1, &next, 10);
                const int buttons = (int)strtol(next + 1, &next, 10);
                mouse = *next == ',' ? next + 1 : next;
                if (buttons & 1) CIAA.pra &= (uint8_t)~0x40;
                else             CIAA.pra |= 0x40;
                if (buttons & 2) chipset.potinp &= (uint16_t)~0x0400;
                else             chipset.potinp |= 0x0400;
                printf("  vbl=%lu mouse %d,%d buttons %d\n",
                       native_frame_counter, mouseDx, mouseDy, buttons);
            }
            const int dx = mouseDx > 2 ? 2 : mouseDx < -2 ? -2 : mouseDx;
            const int dy = mouseDy > 2 ? 2 : mouseDy < -2 ? -2 : mouseDy;
            mouseDx -= dx;
            mouseDy -= dy;
            const uint8_t h = (uint8_t)((chipset.joy0dat & 0xFF) + dx);
            const uint8_t v = (uint8_t)((chipset.joy0dat >> 8) + dy);
            chipset.joy0dat = (uint16_t)(v << 8 | h);
        }

        // frank-amiga: OMEGA_DF0_SWAP=<vbl>:<adf> ejects DF0 at that frame and
        // inserts the other disk 25 frames later (the board's df0 command).
        {
            static long swapAt = -2;
            static char swapPath[512];
            if (swapAt == -2) {
                const char *e = getenv("OMEGA_DF0_SWAP");
                swapAt = -1;
                if (e && sscanf(e, "%ld:%511s", &swapAt, swapPath) != 2) swapAt = -1;
            }
            if (swapAt >= 0 && (long)native_frame_counter == swapAt && df[0].hasDisk) {
                floppyInsert(0);                      /* toggles: eject */
                printf("  vbl=%lu DF0 ejected\n", native_frame_counter);
            }
            if (swapAt >= 0 && (long)native_frame_counter == swapAt + 25 && !df[0].hasDisk) {
                int fd = open(swapPath, O_RDONLY);
                if (fd >= 0) {
                    ADF2MFM(fd, df[0].mfmData);
                    close(fd);
                    floppyInsert(0);
                    printf("  vbl=%lu DF0 inserted %s\n", native_frame_counter, swapPath);
                }
                swapAt = -1;
            }
        }

        // Kickstart only accepts a disk-change once the drive has finished
        // ID mode (df[0].idMode == 0).  Keep trying from insertAt onward
        // until floppyInsert latches hasDisk.
        if (haveImage && insertAt > 0 && it >= insertAt && !df[0].hasDisk &&
            (it % 200) == 0) {
            floppyInsert(0);
            if (df[0].hasDisk) {
                printf("  it=%6ld  disk inserted (idMode=%d)\n",
                       it, df[0].idMode);
                fflush(stdout);
            }
        }

        // The detailed boot-state sampler is useful interactively, but makes
        // automated screenshot regression logs enormous and needlessly slow.
#ifndef OMEGA_SCREENSHOT_REGRESSION
        // PC sampler: after screen is up, print once then stop
        uint32_t exec_base = native_read_u32(4);
        if (native_frame_counter > 20 && (it % 4000) == 0 &&
            exec_base >= 0x13Eu && exec_base < 0xF00000u) {
            uint32_t pc  = m68k_get_reg(NULL, M68K_REG_PC);
            uint32_t a0  = m68k_get_reg(NULL, M68K_REG_A0);
            uint32_t a3  = m68k_get_reg(NULL, M68K_REG_A3);
            uint32_t d0  = m68k_get_reg(NULL, M68K_REG_D0);
            uint32_t sr  = m68k_get_reg(NULL, M68K_REG_SR);
            char dbuf[256];
            uint32_t addr;
            printf("--- it=%ld vbl=%lu INTREQ=%04X INTENA=%04X SR=%04X\n"
                   "    CIA-A: cra=%02X ta=%d crb=%02X tb=%d icr=%02X mask=%02X\n"
                   "    CIA-B: cra=%02X ta=%d crb=%02X tb=%d icr=%02X mask=%02X\n",
                   it, native_frame_counter,
                   chipset.intreqr, chipset.intenar, sr,
                   CIAA.cra, CIAA.ta, CIAA.crb, CIAA.tb, CIAA.icr, CIAA.icrMask,
                   CIAB.cra, CIAB.ta, CIAB.crb, CIAB.tb, CIAB.icr, CIAB.icrMask);
            // Read signal words at (A3)=tc_SigRecvd and (A3-4)=tc_SigWait
            #define RD32(base) ((uint32_t)((low16Meg[(base)]<<24)|(low16Meg[(base)+1]<<16)| \
                                           (low16Meg[(base)+2]<<8)|low16Meg[(base)+3]))
            uint32_t a6  = exec_base;
            printf("    PC=%08X A0=%08X A3=%08X ExecBase(addr4)=%08X D0=%08X\n",
                   pc, a0, a3, a6, d0);
            uint32_t tc_sigrecvd = (a3 < 0xF00000u) ? RD32(a3)   : 0xDEADBEEF;
            uint32_t tc_sigwait  = (a3 >= 4 && (a3-4) < 0xF00000u) ? RD32(a3-4) : 0xDEADBEEF;
            // Level-3 autovector at 0x6C (exception 27)
            uint32_t vec3 = RD32(0x6C);
            // IntVects[3] in ExecBase at a6+0x78
            uint32_t iv3code = 0, iv3data = 0;
            if (a6 < 0xF00000u) {
                iv3code = RD32(a6+0x78);
                iv3data = RD32(a6+0x7C);
            }
            printf("    tc_SigWait=%08X tc_SigRecvd=%08X  vec3=%08X"
                   "  IntVects[3].Code=%08X Data=%08X\n",
                   tc_sigwait, tc_sigrecvd, vec3, iv3code, iv3data);
            // --- Boot state (always prints, uses known absolute addresses) ---
            // ExecBase at A6. TaskReady list: lh_Head at ExecBase+0x196 = 0xC00C1E,
            //   lh_Tail at +0x19A = 0xC00C22 (always 0), lh_TailPred at +0x19E = 0xC00C26.
            // A6+0x114 = dispatched task pointer.
            if (a6 < 0xF00000u) {
                uint32_t tr_head = RD32(a6+0x196), tr_tp = RD32(a6+0x19E);
                printf("  [boot] TaskReady: lh_Head=%08X lh_TailPred=%08X (empty=%s)"
                       "  DispTask=%08X\n",
                       tr_head, tr_tp,
                       (tr_head == a6+0x19A) ? "YES" : "NO",
                       RD32(a6+0x114));
            }
            // Signal target: task at mp_SigTask=0xC06582, tc_SigRecvd at +0x1A = 0xC0659C
            {
                uint32_t sigt = 0xC06582u;
                printf("  [boot] SigTask@C06582: tc_State=%02X tc_SigAlloc=%08X"
                       " tc_SigWait=%08X tc_SigRecvd=%08X\n",
                       (unsigned)low16Meg[sigt+0xF],
                       RD32(sigt+0x12), RD32(sigt+0x16), RD32(sigt+0x1A));
            }
            // Decode ExecBase LVO -0x13E: 6-byte JMP entry at ExecBase-0x13E = 0xC0094A
            if (a6 >= 0x13Eu && a6 < 0xF00000u) {
                uint32_t lvo_addr = a6 - 0x13Eu;
                uint16_t opcode   = (uint16_t)((low16Meg[lvo_addr]<<8)|low16Meg[lvo_addr+1]);
                uint32_t tgt      = RD32(lvo_addr + 2);
                printf("  [boot] Exec LVO -0x13E @%08X: opcode=%04X target=%08X\n",
                       lvo_addr, opcode, tgt);
                // Disassemble target (should be in ROM) so we know the function name
                if (tgt >= 0xF00000u && tgt < 0x1000000u) {
                    addr = tgt;
                    printf("  [boot] LVO -0x13E target code:\n");
                    for (int d = 0; d < 8; d++) {
                        int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                        printf("    %08X: %s\n", addr, dbuf);
                        addr += (len > 0 ? len : 2);
                    }
                }
                fflush(stdout);
            }
            // trackdisk.device tc_SigAlloc (which bits are allocated) at C0485E+0x1E
            #define RD16(base) ((uint16_t)((low16Meg[(base)]<<8)|low16Meg[(base)+1]))
            {
                uint32_t td = 0xC0485Eu;
                printf("  [td] tc_SigAlloc=%08X tc_SigWait=%08X tc_SigRecvd=%08X\n",
                       RD32(td+0x12), RD32(td+0x16), RD32(td+0x1A));
                // Also: trackdisk message port at td+0x?? — look at td+0x54 for pr_MsgPort
                uint32_t td_mp = td + 0x54u;
                printf("  [td] MsgPort@%08X: SigBit=%02X SigTask=%08X Head=%08X TailPred=%08X\n",
                       td_mp, (unsigned)low16Meg[td_mp+0x0F],
                       RD32(td_mp+0x10), RD32(td_mp+0x14), RD32(td_mp+0x1C));
                // DSKLEN and DMACONR state
                uint32_t dsklen_r = (uint32_t)((low16Meg[0xDFF01Du]<<8)|low16Meg[0xDFF01Eu]);
                // DSKPTH at DFF020/DFF022
                printf("  [disk] dmaconr=%04X dsklen=%04X\n",
                       chipset.dmaconr, chipset.dsklen);
            }
            #undef RD16
            // Disassemble key ROM routines (KS 2.04 ROM at 0xFC0000-0xFFFFFF)
            // FEAAF6 = trackdisk.device Wait(bits8+9) return point
            for (int seg = 0; seg < 3; seg++) {
                uint32_t bases[] = {0xFEAA40u, 0xFEAB00u, 0xFC075Au};
                printf("  ROM @ %08X:\n", bases[seg]);
                addr = bases[seg];
                for (int d = 0; d < 32; d++) {
                    int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                    printf("    %08X: %s\n", addr, dbuf);
                    addr += (len > 0 ? len : 2);
                    if (addr > bases[seg] + 0xC0u) break;
                }
                fflush(stdout);
            }
            // Read string at FD3DCA (ln_Name set by FD68B4 — device/library name)
            {
                uint32_t ns = 0xFD3DCAu;
                char nstr[32];
                int nc = 0;
                while (nc < 31 && low16Meg[ns + nc]) { nstr[nc] = (char)low16Meg[ns + nc]; nc++; }
                nstr[nc] = '\0';
                printf("  [boot] FD3DCA name: \"%s\"\n", nstr);
            }
            // Print Process message port state for exec.library @C01570
            // (Process = Task + extra fields; pr_MsgPort at TCB+0x5C)
            {
                uint32_t etask = 0xC01570u;
                uint32_t msgport = etask + 0x5Cu; // pr_MsgPort
                uint8_t  mp_sigbit  = low16Meg[msgport + 0x0F];
                uint32_t mp_sigtask = RD32(msgport + 0x10);
                uint32_t mp_head    = RD32(msgport + 0x14);
                uint32_t mp_tailpred= RD32(msgport + 0x1C);
                printf("  [boot] exec @C01570 pr_MsgPort@%08X: SigBit=%02X SigTask=%08X"
                       " MsgList lh_Head=%08X lh_TailPred=%08X\n",
                       msgport, mp_sigbit, mp_sigtask, mp_head, mp_tailpred);
            }
            // Probe chip-RAM addresses found on exec.library's stack
            // C0485E: new task seen in TASKREADY at hit 25 (romboot/DOS handler?)
            // C01860: appeared at sp+0x172 — data structure
            // C04530: appeared 8 times on stack — device/library base
            // C01E1E: graphics.library base
            for (int p = 0; p < 4; p++) {
                uint32_t probe[] = {0xC0485Eu, 0xC01860u, 0xC04530u, 0xC01E1Eu};
                uint32_t b = probe[p];
                uint8_t  ln_type = low16Meg[b + 0x8];
                uint32_t ln_name = RD32(b + 0xA);
                char pname[24] = "(null)";
                if (ln_name >= 0x100u && ln_name < 0x1000000u) {
                    int nc = 0;
                    while (nc < 23 && low16Meg[ln_name + nc]) {
                        pname[nc] = (char)low16Meg[ln_name + nc]; nc++;
                    }
                    pname[nc] = '\0';
                }
                uint32_t sigwait  = RD32(b + 0x16);
                uint32_t sigrecvd = RD32(b + 0x1A);
                printf("  [probe @%08X] ln_Type=%02X name=\"%s\" SigWait=%08X SigRecvd=%08X\n",
                       b, ln_type, pname, sigwait, sigrecvd);
            }
            // Walk TaskWait list: ExecBase+0x1A4 = lh_Head of TaskWait
            // (TaskReady ends at ExecBase+0x196+14 = ExecBase+0x1A4)
            if (a6 < 0xF00000u) {
                uint32_t tw_head_addr = a6 + 0x1A4;
                uint32_t tw_tail_addr = a6 + 0x1A8; // lh_Tail NULL sentinel
                uint32_t tw_head = RD32(tw_head_addr);
                uint32_t tw_tp   = RD32(a6 + 0x1AC);
                printf("  [boot] TaskWait: lh_Head=%08X lh_TailPred=%08X\n",
                       tw_head, tw_tp);
                uint32_t node = tw_head;
                for (int ntask = 0; ntask < 20 && node >= 0x100u && node < 0x1000000u; ntask++) {
                    if (node == tw_tail_addr) break;
                    uint32_t ln_succ    = RD32(node + 0x0);
                    uint32_t ln_nameptr = RD32(node + 0xA);
                    uint8_t  tc_state   = low16Meg[node + 0xF];
                    uint32_t tc_sw      = RD32(node + 0x16);
                    uint32_t tc_sr      = RD32(node + 0x1A);
                    char tname[20] = "(null)";
                    if (ln_nameptr >= 0x100u && ln_nameptr < 0x1000000u) {
                        int nc = 0;
                        while (nc < 19 && low16Meg[ln_nameptr + nc]) {
                            tname[nc] = (char)low16Meg[ln_nameptr + nc];
                            nc++;
                        }
                        tname[nc] = '\0';
                    }
                    printf("  [wait %2d] @%08X \"%s\" st=%02X SigWait=%08X SigRecvd=%08X\n",
                           ntask, node, tname, tc_state, tc_sw, tc_sr);
                    // Print stack window + scan for ROM return addresses
                    {
                        uint32_t sp_lo = RD32(node + 0x3A); // tc_SPLower
                        uint32_t sp_up = RD32(node + 0x3E); // tc_SPUpper
                        uint32_t sp    = RD32(node + 0x36); // tc_SPReg
                        printf("    stack: SPReg=%08X SPLower=%08X SPUpper=%08X\n",
                               sp, sp_lo, sp_up);
                        if (sp >= 0x100u && sp < 0xF00000u && sp_up >= sp) {
                            uint32_t depth = sp_up - sp;
                            if (depth > 0x200) depth = 0x200;
                            printf("    notable addrs on stack (ROM + chipRAM):\n");
                            for (uint32_t b = 0; b + 3 < depth; b += 2) {
                                uint32_t v = ((uint32_t)(low16Meg[sp+b])<<24)|((uint32_t)(low16Meg[sp+b+1])<<16)|((uint32_t)(low16Meg[sp+b+2])<<8)|low16Meg[sp+b+3];
                                // ROM return addresses or chip-RAM pointers worth seeing
                                if ((v >= 0xF80000u && v < 0x1000000u) ||
                                    (v >= 0xC00000u && v < 0xF00000u))
                                    printf("      sp+0x%03X = %08X\n", b, v);
                            }
                        }
                    }
                    if (ln_succ == tw_tail_addr || ln_succ == 0) break;
                    node = ln_succ;
                }
            }
            // Reply port at 0xC075EE: Node(0xE) + mp_Flags(1)@+E + mp_SigBit(1)@+F + mp_SigTask(4)@+10
            // mp_MsgList starts at +0x14
            printf("  [boot] ReplyPort@C075EE: Flags=%02X SigBit=%02X SigTask=%08X"
                   "  MsgList: lh_Head=%08X lh_TailPred=%08X\n",
                   (unsigned)low16Meg[0xC075EEu+0xE],
                   (unsigned)low16Meg[0xC075EEu+0xF],
                   RD32(0xC075EEu+0x10),
                   RD32(0xC075EEu+0x14),
                   RD32(0xC075EEu+0x1C));
            // 64-bit software timer and IOReq state
            {
                uint32_t dev_data = 0xC06144u;
                uint32_t time_hi  = RD32(dev_data+0x48), time_lo = RD32(dev_data+0x4C);
                printf("  [boot] 64-bit timer: %08X_%08X  target=00014AAE\n",
                       time_hi, time_lo);
                uint16_t ior_cmd  = (uint16_t)((low16Meg[0xC07626u+0x1C]<<8)|low16Meg[0xC07626u+0x1D]);
                printf("  [boot] IOReq@C07626: cmd=%04X flags=%02X error=%02X\n",
                       ior_cmd, low16Meg[0xC07626u+0x1E], low16Meg[0xC07626u+0x1F]);
                // Q1/Q2 state + full IORequest dump
                printf("  [boot] Q1(C061E8): Head=%08X TailPred=%08X  Q2(C061F4): Head=%08X TailPred=%08X\n",
                       RD32(0xC061E8u), RD32(0xC061F0u), RD32(0xC061F4u), RD32(0xC061FCu));
                printf("  [boot] IOReq@C07626 dump (+00..+2C):");
                for (int b = 0; b < 0x30; b += 4)
                    printf(" %08X", RD32(0xC07626u+b));
                printf("\n");
            }
            // I/O handler called every VBL: C062A0 → JMP to ROM FCF61E
            // First word is opcode 0x4EF9 (JMP abs.l); it's in the high 16 bits after RD32
            {
                uint32_t h0 = RD32(0xC062A0u);
                uint32_t ioh_rom = ((h0 >> 16) == 0x4EF9u) ? RD32(0xC062A2u) : 0u;
                printf("  [boot] IOHandler@C062A0: %08X %08X -> ROM JMP %08X\n",
                       h0, RD32(0xC062A4u), ioh_rom);
                if (ioh_rom >= 0xF80000u) {
                    printf("  IOHandler ROM (%08X):\n", ioh_rom);
                    addr = ioh_rom;
                    for (int d = 0; d < 40; d++) {
                        int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                        printf("    %08X: %s\n", addr, dbuf);
                        addr += (len > 0 ? len : 2);
                    }
                    fflush(stdout);
                }
            }
            // fcf3dc: the timer-queue processor (compare current time vs node's target)
            printf("  fcf3dc (0xFCF3DC):\n");
            addr = 0xFCF3DC;
            for (int d = 0; d < 50; d++) {
                int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                printf("    %08X: %s\n", addr, dbuf);
                addr += (len > 0 ? len : 2);
                if (addr > 0xFCF460u) break;
            }
            fflush(stdout);
            // Message at 0xC076E8 (the one being cycled through the reply port)
            printf("  [boot] msg@C076E8 dump:");
            for (int b = 0; b < 0x28; b += 4)
                printf(" %08X", RD32(0xC076E8u+(unsigned)b));
            printf("\n");
            fflush(stdout);
            // ROM code around 0xF820D0 (AddTail/GetMsg observed at F820DE and F82160)
            printf("  ROM@F820C0 (ReplyMsg area):\n");
            addr = 0xF820C0;
            for (int d = 0; d < 30; d++) {
                int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                printf("    %08X: %s\n", addr, dbuf);
                addr += (len > 0 ? len : 2);
            }
            fflush(stdout);
            // Disassemble level-3 handler (more instructions)
            printf("  Lv3 handler (0xF81114):\n");
            addr = 0xF81114;
            for (int d = 0; d < 28; d++) {
                int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                printf("    %08X: %s\n", addr, dbuf);
                addr += len;
            }
            // IntVects (handler uses a6+0x84=COPER, a6+0x90=VERTB, a6+0x9C=BLIT)
            // Each slot: [iv_Data(4)=list head ptr, iv_Code(4)=walker]
            if (a6 < 0xF00000u) {
                uint32_t coper_list = RD32(a6+0x84), coper_code = RD32(a6+0x88);
                uint32_t vertb_list = RD32(a6+0x90), vertb_code = RD32(a6+0x94);
                uint32_t blit_list  = RD32(a6+0x9C), blit_code  = RD32(a6+0xA0);
                printf("  COPER: list=%08X code=%08X\n", coper_list, coper_code);
                printf("  VERTB: list=%08X code=%08X\n", vertb_list, vertb_code);
                printf("  BLIT:  list=%08X code=%08X\n", blit_list,  blit_code);
                // Read VERTB list head → first node
                if (vertb_list && vertb_list < 0xF00000u) {
                    uint32_t lh_Head = RD32(vertb_list);  // first node or tail sentinel
                    uint32_t lh_Tail = RD32(vertb_list+4);
                    printf("  VERTB list: lh_Head=%08X lh_Tail=%08X  (empty=%s)\n",
                           lh_Head, lh_Tail, (lh_Tail == 0 && lh_Head == vertb_list+4) ? "YES" : "NO");
                    // Walk VERTB interrupt server list
                    uint32_t node = lh_Head;
                    for (int ni = 0; ni < 4 && node && node < 0xF00000u; ni++) {
                        uint32_t is_Data = RD32(node+0xE);
                        uint32_t is_Code = RD32(node+0x12);
                        uint32_t ln_Succ = RD32(node+0x0);
                        int is_last = (ln_Succ == vertb_list+4 || ln_Succ == 0);
                        if (is_last)
                            printf("  VERTB[%d]: is_Data=%08X is_Code=%08X (last)\n",
                                   ni, is_Data, is_Code);
                        else
                            printf("  VERTB[%d]: is_Data=%08X is_Code=%08X ln_Succ=%08X\n",
                                   ni, is_Data, is_Code, ln_Succ);
                        if (is_Code && (is_Code >= 0xF80000u || is_Code < 0xF00000u)) {
                            printf("  VERTB[%d].Code (%08X):\n", ni, is_Code);
                            addr = is_Code;
                            for (int d = 0; d < 20; d++) {
                                int len = m68k_disassemble(dbuf, addr, M68K_CPU_TYPE_68000);
                                printf("    %08X: %s\n", addr, dbuf);
                                addr += len;
                            }
                        }
                        // For the last VERTB node, also disassemble its bsr target
                        if (is_last && is_Data < 0xF00000u) {
                            uint32_t dev_base = (is_Data < 0xF00000u) ? RD32(is_Data+0x24) : 0;
                            if (dev_base && dev_base < 0xF00000u) {
                                uint16_t ctr_period = (uint16_t)((low16Meg[dev_base+0x120]<<8)|low16Meg[dev_base+0x121]);
                                uint16_t ctr_cur    = (uint16_t)((low16Meg[dev_base+0x122]<<8)|low16Meg[dev_base+0x123]);
                                printf("  VERTB[%d].dev_base=%08X timer.period=%d cur=%d\n",
                                       ni, dev_base, ctr_period, ctr_cur);
                            }
                        }
                        if (is_last) break;
                        node = ln_Succ;
                    }
                    // ROM disk Q1/Q2 queue state
                    printf("  ROM disk Q1(C061E8): lh_Head=%08X lh_TailPred=%08X (empty=%s)\n",
                           RD32(0xC061E8u), RD32(0xC061F0u),
                           (RD32(0xC061F0u) == 0xC061E8u+4) ? "YES" : "NO");
                    printf("  ROM disk Q2(C061F4): lh_Head=%08X lh_TailPred=%08X (empty=%s)\n",
                           RD32(0xC061F4u), RD32(0xC061FCu),
                           (RD32(0xC061FCu) == 0xC061F4u+4) ? "YES" : "NO");
                }
            }
            #undef RD32
            fflush(stdout);
        }
#endif

        if (dumpEvery > 0 && (it % dumpEvery) == 0) {
            char name[64];
            snprintf(name, sizeof name, "frame%04d.ppm", dumpIndex++);
            native_dump_ppm(name);
            printf("  it=%6ld  vbl=%lu  nonblack_px=%lu  -> %s\n",
                   it, native_frame_counter, native_nonblack_pixels(), name);
            fflush(stdout);
        }
    }

    native_dump_ppm("frame_final.ppm");
    printf("Done. vbl=%lu  nonblack_px=%lu  -> frame_final.ppm\n",
           native_frame_counter, native_nonblack_pixels());
#ifdef HAVE_SDL2
    sdl_display_close();
#endif
    return 0;
}
