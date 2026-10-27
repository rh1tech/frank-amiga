src=open('ref/rp2350-amiga/src/Host.c').read().split('\n')
body='\n'.join(src[453:1085])   # lines 454..1085 (the #endif at 1086 is left out)
assert body.startswith('// ── Core 1: consumer'), body[:30]
def strip_ifblock(text, cond):
    out=[];skip=0;depth=0
    for line in text.split('\n'):
        st=line.strip()
        if skip:
            if st.startswith('#if'): depth+=1
            elif st.startswith('#endif'):
                if depth==0: skip=0; continue
                depth-=1
            continue
        if st==cond: skip=1; depth=0; continue
        out.append(line)
    return '\n'.join(out)
body=strip_ifblock(body,'#if OMEGA_RTG')
def rep(a,b):
    global body
    assert a in body, a[:60]
    body=body.replace(a,b)
rep("""            case MSG_ROW_END:
                c1RowEnd(row);
                length = 1;
                break;""","""            case MSG_ROW_END:
                c1RowEnd(row);
                length = 1;
                break;
            case MSG_AUDIO: {
                // frank-amiga: Paula's line record (omega/Audio.h), mixed
                // here (omega/AudioMix.c) into the I2S output.
                const uint32_t n = header >> 23;
                denise_audio_lines++;
                if (n <= AUDIO_LINE_MAX_WORDS) {
                    uint32_t words[AUDIO_LINE_MAX_WORDS];
                    uint32_t s = ringSlot(tail);
                    for (uint32_t i = 0; i < n; ++i) {
                        if (++s == RING_WORDS) s = 0;
                        words[i] = ring[s];
                    }
                    if (words[0] & AUDIO_LINE_LONG) denise_audio_long++;
                    audioMixLine(words, (int)n);
                }
                length = 1 + n;
                break;
            }""")
rep("""            default:
                panic("host: bad core-1 ring message %08lx",
                      (unsigned long)header);
            }""","""            default:
                // frank-amiga: a corrupt stream (lost link data). Drop
                // everything queued; stream.c resynchronises at a frame.
                denise_stream_corrupt(header);
                head = __atomic_load_n(&ring_head, __ATOMIC_ACQUIRE);
                tail = head;
                __atomic_store_n(&ring_tail, tail, __ATOMIC_RELEASE);
                c1_image = NULL;
                continue;
            }""")
# ── frank-amiga: dual playfield ────────────────────────────────────────────
rep("""static inline void C1_FUNC(c1Copy)(uint8_t *line,""", """// frank-amiga: dual playfield. A DPF block is split into its playfields
// (planes 1/3/5 -> PF1, colours 0-7; planes 2/4/6 -> PF2, colours 8-15),
// each kept in a row buffer at its own scroll offset; they are combined by
// priority when the row is flushed (before anything else touches it).
static uint8_t dpf_buf[DVI_DIRECT_IMAGE_WIDTH];  // PF1 | PF2 << 3 per column
static int dpf_y = -1, dpf_min, dpf_max;
static int c1_dpf_delta;          // PF2 offset from PF1, image columns
static bool c1_pf2pri;
static void C1_FUNC(c1Record)(int y, int x0, int x1);

// Colour numbers of a 6-bit DPF pixel: its playfields (PF1 in bits 0-2,
// PF2 in bits 3-5 of the row buffer) and the colour of a buffer value by
// priority (PF2PRI: 1 = PF2 in front); dpf_comp6 is the same for a raw
// 6-bit pixel (same scroll, no buffer).
static uint8_t dpf_lut1[64], dpf_lut2[64], dpf_comp[2][64], dpf_comp6[2][64];
static void C1_FUNC(dpfTables)(void) {
    for (int v = 0; v < 64; ++v) {
        const uint8_t a = (uint8_t)((v & 1) | ((v >> 1) & 2) | ((v >> 2) & 4));
        const uint8_t b = (uint8_t)(((v >> 1) & 1) | ((v >> 2) & 2) | ((v >> 3) & 4));
        dpf_lut1[v] = a;
        dpf_lut2[v] = (uint8_t)(b << 3);
        dpf_comp6[0][v] = a ? a : (b ? (uint8_t)(8u + b) : 0);
        dpf_comp6[1][v] = b ? (uint8_t)(8u + b) : a;
        const uint8_t pa = (uint8_t)(v & 7), pb = (uint8_t)(v >> 3);
        dpf_comp[0][v] = pa ? pa : (pb ? (uint8_t)(8u + pb) : 0);
        dpf_comp[1][v] = pb ? (uint8_t)(8u + pb) : pa;
    }
}

static void __attribute__((noinline)) C1_FUNC(dpfFlush)(void) {
    if (dpf_y < 0)
        return;
    const int y = dpf_y;
    dpf_y = -1;
    if (!c1_image || dpf_max <= dpf_min)
        return;
    uint8_t *line = c1_image + y * DVI_DIRECT_IMAGE_WIDTH;
    const uint8_t *comp = dpf_comp[c1_pf2pri];
    for (int x = dpf_min; x < dpf_max; ++x)
        line[x] = comp[dpf_buf[x]];
    memset(dpf_buf + dpf_min, 0, (size_t)(dpf_max - dpf_min));
    const bool ham = c1_ham;
    c1_ham = false;
    c1Record(y, dpf_min, dpf_max);
    c1_ham = ham;
}

// Clips [x, x + width) of a block to the image; returns the first index.
static inline int C1_FUNC(dpfClip)(int x, int width, int *end) {
    int i0 = x < 0 ? -x : 0;
    int i1 = width;
    if (x + i1 > DVI_DIRECT_IMAGE_WIDTH)
        i1 = DVI_DIRECT_IMAGE_WIDTH - x;
    *end = i1;
    return i0;
}

static void C1_FUNC(dpfPlace)(int y, int image_x, const uint8_t *pixels,
                              int width) {
    static bool ready;
    if (!ready) {
        dpfTables();
        ready = true;
    }
    if (y != dpf_y)
        dpfFlush();
    // Playfields at the same scroll (the usual case): combine straight into
    // the row.
    if (c1_dpf_delta == 0 && dpf_y < 0) {
        int i1;
        const int i0 = dpfClip(image_x, width, &i1);
        if (i1 <= i0)
            return;
        uint8_t *line = c1_image + y * DVI_DIRECT_IMAGE_WIDTH + image_x;
        const uint8_t *comp = dpf_comp6[c1_pf2pri];
        for (int i = i0; i < i1; ++i)
            line[i] = comp[pixels[i]];
        const bool ham = c1_ham;
        c1_ham = false;
        c1Record(y, image_x + i0, image_x + i1);
        c1_ham = ham;
        return;
    }
    if (y != dpf_y) {
        dpf_y = y;
        dpf_min = DVI_DIRECT_IMAGE_WIDTH;
        dpf_max = 0;
    }
    int i1;
    int i0 = dpfClip(image_x, width, &i1);
    if (i1 > i0) {
        uint8_t *buf = dpf_buf + image_x;
        for (int i = i0; i < i1; ++i)
            buf[i] = (uint8_t)((buf[i] & 0x38u) | dpf_lut1[pixels[i]]);
        if (image_x + i0 < dpf_min) dpf_min = image_x + i0;
        if (image_x + i1 > dpf_max) dpf_max = image_x + i1;
    }
    const int x2 = image_x + c1_dpf_delta;
    i0 = dpfClip(x2, width, &i1);
    if (i1 > i0) {
        uint8_t *buf = dpf_buf + x2;
        for (int i = i0; i < i1; ++i)
            buf[i] = (uint8_t)((buf[i] & 0x07u) | dpf_lut2[pixels[i]]);
        if (x2 + i0 < dpf_min) dpf_min = x2 + i0;
        if (x2 + i1 > dpf_max) dpf_max = x2 + i1;
    }
}

static inline void C1_FUNC(c1Copy)(uint8_t *line,""")
# c1Place: DPF blocks go to the playfield buffers (full-width layout only)
rep("""    int image_x = x - c1_rotation;
    if (image_x < 0)
        image_x += HOST_RASTER_W;
    int first = HOST_RASTER_W - image_x;""", """    int image_x = x - c1_rotation;
    if (image_x < 0)
        image_x += HOST_RASTER_W;
    if (c1_dpf_block) {
        dpfPlace(y, image_x, pixels, width);
        return;
    }
    if (dpf_y >= 0)
        dpfFlush();
    int first = HOST_RASTER_W - image_x;""")
rep("""static uint16_t c1_palette[32];   // current colours, 0x0RGB""", """static uint16_t c1_palette[32];   // current colours, 0x0RGB
static bool c1_dpf_block;         // frank-amiga: the block being placed is DPF
static void dpfFlush(void);""")
# flush before anything else draws on / closes the row
for fn in ['static void C1_FUNC(c1Sprite)(int row, int x, uint32_t extra, uint32_t ab,\n                              uint32_t cd) {',
           'static void C1_FUNC(c1RowSame)(int row) {',
           'static void C1_FUNC(c1RowEnd)(int row) {',
           'static void C1_FUNC(c1End)(uint16_t border) {',
           'static void C1_FUNC(c1Colour)(int row, int x, int reg, uint16_t value) {']:
    rep(fn, fn+'\n    dpfFlush();')
rep("""            case MSG_HIRES: {
                const int count = (int)(header >> MSG_RUN_SHIFT & 0xfu) + 1;""", """            case MSG_DPF: {
                const uint32_t st = header >> 23;
                dpfFlush();
                c1_dpf_delta = 2 * ((int)((st & 31u) ^ 16u) - 16);
                c1_pf2pri = (st >> 5) & 1u;
                length = 1;
                break;
            }
            case MSG_HIRES: {
                const int count = (int)(header >> MSG_RUN_SHIFT & 0xfu) + 1;
                c1_dpf_block = (header & MSG_RUN_DPF) != 0;""")
rep("""            case MSG_LORES: {
                const int count = (int)(header >> MSG_RUN_SHIFT & 0xfu) + 1;""", """            case MSG_LORES: {
                const int count = (int)(header >> MSG_RUN_SHIFT & 0xfu) + 1;
                c1_dpf_block = (header & MSG_RUN_DPF) != 0;""")

head=open('master/src/denise/denise_c1_head.inc').read()
open('master/src/denise/denise_c1.c','w').write(head+body+'\n')
print(len(body.split('\n')),'lines')
# frank-amiga entry point for the planar tables (static upstream).
with open('master/src/denise/denise_c1.c','a') as f:
    f.write('\nvoid denise_c1_init(void) {\n    hostInitPlanarTables();\n}\n')
