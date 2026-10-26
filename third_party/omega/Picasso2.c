//
//  Picasso2.c
//  Omega
//
//  The Picasso II+'s GD5428 (see Picasso2.h). What is emulated is what the
//  Amiga drivers use: the VGA/Cirrus registers that set a packed-pixel mode
//  (8, 15, 16 and 24 bits), the DAC with its hidden register, the BitBLT
//  engine (copies, fills, colour expansion from VRAM or from the CPU, the
//  raster operations), the hardware cursor, the monitor switch and the
//  vertical-retrace status. The VGA's planar and text modes are not.
//
//  Blits run to completion when started, as in WinUAE, so the engine is
//  never seen busy - except while it waits for source data from the CPU.
//

#include "Picasso2.h"
#include "Chipset.h"
#include <string.h>
#ifdef P2_DEBUG_BLITS
#include <stdio.h>
int p2DebugBlits;              // native: OMEGA_P2_DEBUG=1 logs every blit
#endif

// ── State ────────────────────────────────────────────────────────────────
static uint8_t *vram;
static uint32_t vramSize, vramMask;

// The register files, the DAC and the blitter, in memory the host gives
// (P2_STATE_BYTES; PSRAM on the board, where SRAM is full).
typedef struct {
    uint8_t seq[0x20], crt[0x40], gr[0x40], attr[0x20];
    // DAC: 256 entries plus the two cursor colours, 6 bits each.
    uint8_t dac[256 + 2][3];
    struct {
        unsigned width, height;     // bytes, rows
        unsigned dpitch, spitch;
        uint32_t dst, src;
        uint8_t mode, rop, skip;
        unsigned bpp;               // 1 or 2
        // A blit fed by the CPU: its source rows as they arrive.
        unsigned row, rowBytes, have;
        uint8_t buf[2048];
    } blt;
} P2State;
_Static_assert(sizeof(P2State) <= P2_STATE_BYTES, "P2_STATE_BYTES too small");
static P2State *st;
#define seq  (st->seq)
#define crt  (st->crt)
#define gr   (st->gr)
#define attr (st->attr)
#define dac  (st->dac)
#define blt  (st->blt)

static uint8_t seqIndex, crtIndex, grIndex;
static uint8_t attrIndex, attrFlip;               // flip: 1 = data next
static uint8_t misc;
static uint8_t cursorXLow, cursorYLow;            // SR10/SR11 bits 2-0
static uint8_t monitorRtg;                        // switch: the card's picture
static uint8_t dacWrite, dacRead, dacComponent, dacReadComponent;
static uint8_t pixelMask = 0xFF;
static uint8_t hdr, hdrReads;                     // hidden DAC register

#define CURSOR_DAC 256                            // colours 0 and 1

volatile uint8_t p2CpuBlt;

// ── Ports ────────────────────────────────────────────────────────────────
// Register board offset = ISA port; an offset with bit 12 set reaches the
// odd port of its pair ("0x13C8" is 0x3C9). Offsets 0x8000 up are the
// board's control: a write at 0xA000 shows the card, 0xB000 the Amiga.
static unsigned port_of(uint32_t offset) {
    if (offset & 0x1000)
        return (offset | 1) & 0x0FFF;
    return offset & 0xFFFF;
}

// ── The blitter ──────────────────────────────────────────────────────────
static uint8_t rop(uint8_t code, uint8_t s, uint8_t d) {
    switch (code) {
    case 0x00: return 0;
    case 0x05: return s & d;
    case 0x06: return d;
    case 0x09: return s & ~d;
    case 0x0B: return ~d;
    case 0x0D: return s;
    case 0x0E: return 0xFF;
    case 0x50: return ~s & d;
    case 0x59: return s ^ d;
    case 0x6D: return s | d;
    case 0x90: return ~(s | d);
    case 0x95: return ~(s ^ d);
    case 0xAD: return s | ~d;
    case 0xD0: return ~s;
    case 0xD6: return ~s | d;
    case 0xDA: return ~(s & d);
    }
    return s;
}

#define VRAM_MASK vramMask

static void put(uint32_t a, uint8_t s) {
    a &= VRAM_MASK;
    vram[a] = rop(blt.rop, s, vram[a]);
}

// One row of colour expansion: bits MSB first from `bits`, starting at bit
// `first`; set bits get the foreground, clear ones the background or
// (transparency) nothing. Pixels before the left-edge skip are not drawn.
static void expand_row(uint32_t d, const uint8_t *bits, unsigned first) {
    const unsigned pixels = blt.width / blt.bpp;
    const uint8_t fg[2] = { gr[0x01], gr[0x11] };
    const uint8_t bg[2] = { gr[0x00], gr[0x10] };
    const int transparent = blt.mode & 0x08;
    for (unsigned x = blt.skip; x < pixels; x++) {
        const unsigned bit = first + x - blt.skip;
        const int set = (bits[bit >> 3] >> (7 - (bit & 7))) & 1;
        if (!set && transparent)
            continue;
        for (unsigned i = 0; i < blt.bpp; i++)
            put(d + x * blt.bpp + i, set ? fg[i] : bg[i]);
    }
}

// The bytes a blit may write: its rows, from the first to the last.
static void blit_dirty(void) {
    const uint32_t span = (blt.height - 1) * blt.dpitch + blt.width;
    p2HostDirty((blt.mode & 0x01) ? (blt.dst - span + 1) & VRAM_MASK : blt.dst,
                span);
}

static void blit_vram(void) {
    const int backwards = blt.mode & 0x01;
    for (unsigned y = 0; y < blt.height; y++) {
        const uint32_t d = backwards ? blt.dst - y * blt.dpitch
                                     : blt.dst + y * blt.dpitch;
        if (blt.mode & 0x40) {
            // 8x8 pattern; bits 2-0 of the source address pick its first row
            const uint32_t base = blt.src & ~7u & VRAM_MASK;
            const unsigned row = ((blt.src & 7) + y) & 7;
            if (blt.mode & 0x80) {
                const uint8_t bits = vram[base + row];
                uint8_t line[2048 / 8];
                for (unsigned i = 0; i < sizeof line; i++)
                    line[i] = bits;
                const uint8_t skip = blt.skip;
                blt.skip = 0;           // the edge skip is expansion's
                expand_row(d, line, 0);
                blt.skip = skip;
            } else {
                const uint32_t prow = base + row * 8u * blt.bpp;
                for (unsigned x = 0; x < blt.width; x++)
                    put(d + x, vram[(prow + x % (8u * blt.bpp)) & VRAM_MASK]);
            }
        } else if (blt.mode & 0x80) {
            // packed 1-bit rows, byte-padded and contiguous (the source
            // pitch is not used)
            const unsigned pixels = blt.width / blt.bpp;
            const unsigned rowBytes = (pixels + 7) / 8;
            uint8_t line[2048 / 8 + 1];
            for (unsigned i = 0; i < rowBytes; i++)
                line[i] = vram[(blt.src + y * rowBytes + i) & VRAM_MASK];
            expand_row(d, line, blt.skip);
        } else {
            const uint32_t s = backwards ? blt.src - y * blt.spitch
                                         : blt.src + y * blt.spitch;
            for (unsigned x = 0; x < blt.width; x++) {
                const uint32_t o = backwards ? 0u - x : x;
                put(d + o, vram[(s + o) & VRAM_MASK]);
            }
        }
    }
}

static void blit_start(void) {
    blt.width  = (gr[0x20] | (gr[0x21] & 0x07) << 8) + 1u;
    blt.height = (gr[0x22] | (gr[0x23] & 0x03) << 8) + 1u;
    blt.dpitch = gr[0x24] | (gr[0x25] & 0x1F) << 8;
    blt.spitch = gr[0x26] | (gr[0x27] & 0x1F) << 8;
    blt.dst = (gr[0x28] | gr[0x29] << 8 | (gr[0x2A] & 0x1F) << 16) & VRAM_MASK;
    blt.src = (gr[0x2C] | gr[0x2D] << 8 | (gr[0x2E] & 0x1F) << 16) & VRAM_MASK;
    blt.mode = gr[0x30];
    blt.rop = gr[0x32];
    blt.skip = gr[0x2F] & 7;
    blt.bpp = (blt.mode & 0x10) ? 2 : 1;
#ifdef P2_DEBUG_BLITS
    if (p2DebugBlits)
        printf("P2 blt mode %02x rop %02x %ux%u dst %06lx/%u src %06lx/%u fg %02x bg %02x skip %u\n",
               blt.mode, blt.rop, blt.width, blt.height,
               (unsigned long)blt.dst, blt.dpitch, (unsigned long)blt.src,
               blt.spitch, gr[0x01], gr[0x00], blt.skip);
#endif
    if (blt.mode & 0x04) {
        // the source comes from the CPU: expansion rows byte-padded,
        // plain rows padded to 32 bits
        blt.row = blt.have = 0;
        blt.rowBytes = (blt.mode & 0x80) ? (blt.width / blt.bpp + 7) / 8
                                         : (blt.width + 3) & ~3u;
        if (blt.rowBytes > sizeof blt.buf)
            return;
        p2CpuBlt = 1;
        return;
    }
    blit_vram();
    blit_dirty();
}

void p2BltFeed(uint8_t value) {
    if (!p2CpuBlt)
        return;
    blt.buf[blt.have++] = value;
    if (blt.have < blt.rowBytes)
        return;
    const uint32_t d = blt.dst + blt.row * blt.dpitch;
    if (blt.mode & 0x80)
        expand_row(d, blt.buf, blt.skip);
    else
        for (unsigned x = 0; x < blt.width; x++)
            put(d + x, blt.buf[x]);
    // marked as each row lands: a host copying VRAM may have taken the
    // area since the blit started
    p2HostDirty(d & VRAM_MASK, blt.width);
    blt.have = 0;
    if (++blt.row >= blt.height) {
        p2CpuBlt = 0;
#ifdef P2_DEBUG_BLITS
        if (p2DebugBlits)
            printf("P2 CPU blit done\n");
#endif
    }
}

// ── Registers ────────────────────────────────────────────────────────────
static int vretrace(void) {
    const unsigned v = (chipset.vposr & 1u) << 8 | chipset.vhposr >> 8;
    return v < 8;
}

static int blanking(void) {
    const unsigned v = (chipset.vposr & 1u) << 8 | chipset.vhposr >> 8;
    const unsigned h = chipset.vhposr & 0xFF;
    return v < 26 || h >= 0xC0;
}

// With SR12 bit 1 the DAC's entries 0x00 and 0x0F are the cursor colours.
static unsigned dac_entry(uint8_t index) {
    if (seq[0x12] & 0x02) {
        if (index == 0x00) return CURSOR_DAC;
        if (index == 0x0F) return CURSOR_DAC + 1;
    }
    return index;
}

uint8_t p2RegRead(uint32_t offset) {
    if (offset & 0x8000)
        return 0xFF;
    switch (port_of(offset)) {
    case 0x3C0: return attrIndex;
    case 0x3C1: return attr[attrIndex & 0x1F];
    case 0x3C2: return 0x00;                     // no vertical interrupt
    case 0x3C4: return seqIndex;
    case 0x3C5: {
        const unsigned i = seqIndex & 0x1F;
        if (i == 0x06)
            return seq[0x06] == 0x12 ? 0x12 : 0x0F;
        return seq[i];
    }
    case 0x3C6:
        if (hdrReads >= 4)
            return hdr;
        hdrReads++;
        return pixelMask;
    case 0x3C7: hdrReads = 0; return dacComponent ? 0x00 : 0x03;
    case 0x3C8: hdrReads = 0; return dacWrite;
    case 0x3C9: {
        hdrReads = 0;
        const unsigned e = dac_entry(dacRead);
        const uint8_t v = dac[e][dacReadComponent];
        if (++dacReadComponent == 3) {
            dacReadComponent = 0;
            dacRead++;
        }
        return v;
    }
    case 0x3CA: return 0x00;
    case 0x3CC: return misc;
    case 0x3CE: return grIndex;
    case 0x3CF:
        if ((grIndex & 0x3F) == 0x31)
            return (gr[0x31] & ~0x0Bu) | (p2CpuBlt ? 0x09 : 0x00);
        return gr[grIndex & 0x3F];
    case 0x3B4:
    case 0x3D4: return crtIndex;
    case 0x3B5:
    case 0x3D5:
        switch (crtIndex & 0x3F) {
        case 0x24: return attrFlip ? 0x80 : 0x00;
        case 0x26: return attrIndex & 0x3F;
        case 0x27: return 0x98;                  // GD5428
        }
        return crt[crtIndex & 0x3F];
    case 0x3BA:
    case 0x3DA:
        attrFlip = 0;
        return (vretrace() ? 0x08 : 0x00) | (blanking() ? 0x01 : 0x00);
    }
    return 0xFF;
}

void p2RegWrite(uint32_t offset, uint8_t value) {
    if (offset & 0x8000) {
        switch ((offset >> 12) & 0xF) {
        case 0xA: monitorRtg = 1; break;
        case 0xB: monitorRtg = 0; break;
        }
        return;
    }
    switch (port_of(offset)) {
    case 0x3C0:
        if (!attrFlip)
            attrIndex = value;
        else
            attr[attrIndex & 0x1F] = value;
        attrFlip ^= 1;
        break;
    case 0x3C2: misc = value; break;
    case 0x3C4:
        seqIndex = value;
        // SR10/SR11: the cursor position's low bits ride in the index
        if ((value & 0x1F) == 0x10) cursorXLow = value >> 5;
        if ((value & 0x1F) == 0x11) cursorYLow = value >> 5;
        break;
    case 0x3C5: seq[seqIndex & 0x1F] = value; break;
    case 0x3C6:
        if (hdrReads >= 4)
            hdr = value;
        else
            pixelMask = value;
        hdrReads = 0;
        break;
    case 0x3C7: hdrReads = 0; dacRead = value; dacReadComponent = 0; break;
    case 0x3C8: hdrReads = 0; dacWrite = value; dacComponent = 0; break;
    case 0x3C9: {
        hdrReads = 0;
        dac[dac_entry(dacWrite)][dacComponent] = value & 0x3F;
        if (++dacComponent == 3) {
            dacComponent = 0;
            dacWrite++;
        }
        break;
    }
    case 0x3CE: grIndex = value; break;
    case 0x3CF: {
        const unsigned i = grIndex & 0x3F;
        if (i == 0x31) {
            if (value & 0x04) {          // reset: stop the engine
#ifdef P2_DEBUG_BLITS
                if (p2DebugBlits && p2CpuBlt)
                    printf("P2 reset during a CPU blit: row %u of %u, %u of %u bytes\n",
                           blt.row, blt.height, blt.have, blt.rowBytes);
#endif
                p2CpuBlt = 0;
            }
            gr[0x31] = value & ~0x02u;
            if ((value & 0x02) && !p2CpuBlt)
                blit_start();
            break;
        }
        gr[i] = value;
        break;
    }
    case 0x3B4:
    case 0x3D4: crtIndex = value; break;
    case 0x3B5:
    case 0x3D5: {
        const unsigned i = crtIndex & 0x3F;
        // CR11 bit 7 protects CR0-CR7 (all but CR07's line-compare bit)
        if ((crt[0x11] & 0x80) && i <= 7) {
            if (i == 7)
                crt[7] = (crt[7] & ~0x10u) | (value & 0x10);
            break;
        }
        if (i != 0x24 && i != 0x26 && i != 0x27)
            crt[i] = value;
        break;
    }
    }
}

// ── Power-on ─────────────────────────────────────────────────────────────
void p2Reset(void) {
    monitorRtg = 0;
    p2CpuBlt = 0;
    gr[0x31] = 0;
}

void p2Init(uint8_t *memory, uint32_t size, void *state) {
    st = state;
    vram = memory;
    vramSize = size;
    vramMask = size - 1;
    memset(seq, 0, sizeof seq);
    memset(crt, 0, sizeof crt);
    memset(gr, 0, sizeof gr);
    memset(attr, 0, sizeof attr);
    memset(dac, 0, sizeof dac);
    seq[0x01] = 0x20;                 // screen off
    seq[0x06] = 0x0F;                 // extensions locked
    pixelMask = 0xFF;
    hdr = hdrReads = 0;
    attrFlip = 0;
    p2Reset();
}

// ── Display ──────────────────────────────────────────────────────────────
int p2Showing(void) {
    return monitorRtg && !(seq[0x01] & 0x20) && (attrIndex & 0x20) &&
           (seq[0x07] & 0x01);
}

void p2Geometry(int *width, int *height) {
    int w = (crt[0x01] + 1) * 8;
    int h = (crt[0x12] | (crt[0x07] & 0x02) << 7 | (crt[0x07] & 0x40) << 3) + 1;
    if (crt[0x17] & 0x04)             // vertical values halved
        h *= 2;
    if (crt[0x09] & 0x80)             // double scan
        h /= 2;
    if (w > 1280) w = 1280;
    if (h > 1024) h = 1024;
    *width = w;
    *height = h;
}

static inline uint32_t dac_rgb(unsigned e) {
    const uint8_t *c = dac[e];
    return (uint32_t)(c[0] << 2 | c[0] >> 4) << 16 |
           (uint32_t)(c[1] << 2 | c[1] >> 4) << 8 |
           (uint32_t)(c[2] << 2 | c[2] >> 4);
}

static void cursor_line(int y, uint32_t *out, int width) {
    if (!(seq[0x12] & 0x01))
        return;
    const int big = seq[0x12] & 0x04;
    const int size = big ? 64 : 32;
    const int cx = seq[0x10] << 3 | cursorXLow;
    const int cy = seq[0x11] << 3 | cursorYLow;
    const int row = y - cy;
    if (row < 0 || row >= size)
        return;
    const uint32_t base = vramSize - 0x4000u +
                          (uint32_t)(seq[0x13] & (big ? 0x3C : 0x3F)) * 256u;
    const uint8_t *p0 = &vram[big ? base + row * 16u : base + row * 4u];
    const uint8_t *p1 = big ? p0 + 8 : p0 + 128;
    for (int x = 0; x < size; x++) {
        const int sx = cx + x;
        if (sx >= width)
            break;
        const int b0 = (p0[x >> 3] >> (7 - (x & 7))) & 1;
        const int b1 = (p1[x >> 3] >> (7 - (x & 7))) & 1;
        if (!b0 && !b1)
            continue;
        if (!b1)
            out[sx] ^= 0x00FFFFFFu;
        else
            out[sx] = dac_rgb(CURSOR_DAC + b0);
    }
}

uint32_t p2Rgb(unsigned entry) {
    return dac_rgb(entry < 256 ? entry : CURSOR_DAC + (entry & 1));
}

void p2Display(P2Display *d) {
    p2Geometry(&d->width, &d->height);
    d->pitch = (crt[0x13] | (crt[0x1B] & 0x10) << 4) * 8u;
    d->start = (crt[0x0D] | crt[0x0C] << 8 | (crt[0x1B] & 0x01) << 16 |
                (crt[0x1B] & 0x0C) << 15) * 4u & vramMask;
    const unsigned depth = (seq[0x07] >> 1) & 3;
    d->bytesPerPixel = depth == 0 ? 1 : depth == 2 ? 3 : 2;
    d->rgb565 = (hdr & 0xCF) == 0xC1;
    d->pixelMask = pixelMask;
    d->cursorOn = seq[0x12] & 0x01;
    const int big = seq[0x12] & 0x04;
    d->cursorSize = big ? 64 : 32;
    d->cursorX = seq[0x10] << 3 | cursorXLow;
    d->cursorY = seq[0x11] << 3 | cursorYLow;
    d->cursorPattern = vramSize - 0x4000u +
                       (uint32_t)(seq[0x13] & (big ? 0x3C : 0x3F)) * 256u;
}

void p2RenderLine(int y, uint32_t *out) {
    int width, height;
    p2Geometry(&width, &height);
    const unsigned pitch = (crt[0x13] | (crt[0x1B] & 0x10) << 4) * 8u;
    const uint32_t start = (crt[0x0D] | crt[0x0C] << 8 |
                            (crt[0x1B] & 0x01) << 16 |
                            (crt[0x1B] & 0x0C) << 15) * 4u;
    uint32_t a = start + (uint32_t)y * pitch;
    const unsigned depth = (seq[0x07] >> 1) & 3;
    if (depth == 0) {                                 // 8 bits, CLUT
        for (int x = 0; x < width; x++)
            out[x] = dac_rgb(vram[(a + x) & VRAM_MASK] & pixelMask);
    } else if (depth == 2) {                          // 24 bits, B,G,R
        for (int x = 0; x < width; x++, a += 3)
            out[x] = (uint32_t)vram[(a + 2) & VRAM_MASK] << 16 |
                     (uint32_t)vram[(a + 1) & VRAM_MASK] << 8 |
                     vram[a & VRAM_MASK];
    } else if ((hdr & 0xCF) == 0xC1) {                // 16 bits, 5:6:5 LE
        for (int x = 0; x < width; x++, a += 2) {
            const unsigned p = vram[a & VRAM_MASK] |
                               vram[(a + 1) & VRAM_MASK] << 8;
            const unsigned r = p >> 11, g = (p >> 5) & 0x3F, b = p & 0x1F;
            out[x] = (r << 3 | r >> 2) << 16 | (g << 2 | g >> 4) << 8 |
                     (b << 3 | b >> 2);
        }
    } else {                                          // 15 bits, 1:5:5:5 LE
        for (int x = 0; x < width; x++, a += 2) {
            const unsigned p = vram[a & VRAM_MASK] |
                               vram[(a + 1) & VRAM_MASK] << 8;
            const unsigned r = (p >> 10) & 0x1F, g = (p >> 5) & 0x1F,
                           b = p & 0x1F;
            out[x] = (r << 3 | r >> 2) << 16 | (g << 3 | g >> 2) << 8 |
                     (b << 3 | b >> 2);
        }
    }
    cursor_line(y, out, width);
}
