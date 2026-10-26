//
//  KickSmash.c
//  Omega
//
//  The target side of the KickSmash32 ROM-bus message protocol (see
//  KickSmash.h), as the switcher's transport (amiga/sm_msg_core.c in the
//  KickSmash sources) drives it:
//
//  request  32-bit reads at ROM base + (word << shift), shift 2 (32-bit
//           ROM) or 1 (16-bit): magic 0x0204 0x1017 0x0119 0x0117, length,
//           command, the argument words, a pad word if their count is odd,
//           and the CRC-32 (STM32 polynomial) of length, command and
//           arguments, high word first;
//  reply    triggered by a read at ROM base + 0x1554, which already returns
//           the first long; every ROM read after it returns the next long
//           (KickSmash drives the bus whatever the address): magic, length,
//           status, the data padded to a long, then the CRC of length,
//           status and data.
//
//  Only reads by code running from RAM take part: the transport copies
//  itself there first, and the ROM's own code (Kickstart reading its
//  tables) must neither start a request nor consume a reply.
//

#include "KickSmash.h"
#include "m68k.h"
#include <string.h>
#include <stdio.h>

// Commands and status (fw/smash_cmd.h)
#define KS_CMD_NOP          0x01
#define KS_CMD_ID           0x02
#define KS_CMD_SET          0x07
#define KS_CMD_GET          0x08
#define KS_CMD_BANK_INFO    0x20
#define KS_CMD_BANK_SET     0x21
#define KS_CMD_BANK_MERGE   0x22
#define KS_CMD_BANK_NAME    0x23
#define KS_CMD_BANK_LRESET  0x24
#define KS_SET_NAME         0x0100
#define KS_SET_NV           0x0200
#define KS_GET_NV           0x0200
#define KS_BANK_SETCURRENT  0x0100
#define KS_BANK_SETRESET    0x0200
#define KS_BANK_SETPOWERON  0x0400
#define KS_BANK_REBOOT      0x8000
#define KS_BANK_UNMERGE     0x0100
#define KS_STATUS_OK        0x0000
#define KS_STATUS_FAIL      0x0100
#define KS_STATUS_CRC       0x0200
#define KS_STATUS_UNKCMD    0x0300
#define KS_STATUS_BADARG    0x0400
#define KS_STATUS_BADLEN    0x0500

#define KS_REPLY_TRIGGER    0x1554
#define KS_ROM_WINDOW       0x80000u

static const uint16_t magic[4] = { 0x0204, 0x1017, 0x0119, 0x0117 };

volatile uint8_t kickSmashBusy;
static KickSmashState *ks;
static int switch_bank = -1;        // requested by the current reply
static int switch_ready = -1;       // ...taken once the reply is read

void kickSmashInit(KickSmashState *state) {
    ks = state;
    memset(ks, 0, sizeof *ks);
    ks->bi.valid = 1;
    ks->bi.bank_nextreset = 0xff;
    memset(ks->bi.longreset_seq, 0xff, sizeof ks->bi.longreset_seq);
    strcpy(ks->board_name, "Omega");
    kickSmashBusy = 0;
    switch_bank = switch_ready = -1;
}

KickSmashState *kickSmashState(void) {
    return ks;
}

static uint32_t crc32be(uint32_t crc, const uint8_t *p, unsigned n) {
    while (n--) {
        crc ^= (uint32_t)*p++ << 24;
        for (unsigned b = 0; b < 8; b++)
            crc = (crc << 1) ^ ((crc & 0x80000000u) ? 0x04c11db7u : 0);
    }
    return crc;
}

static void put16(uint8_t *p, unsigned *n, unsigned v) {
    p[(*n)++] = (uint8_t)(v >> 8);
    p[(*n)++] = (uint8_t)v;
}

static void put32(uint8_t *p, unsigned *n, uint32_t v) {
    put16(p, n, v >> 16);
    put16(p, n, v & 0xffff);
}

// Builds the reply: header, data, padding, CRC.
static void reply(unsigned status, const void *data, unsigned len) {
    unsigned n = 0;
    if (len > sizeof ks->reply - 20)
        len = 0, status = KS_STATUS_FAIL;
    for (unsigned i = 0; i < 4; i++)
        put16(ks->reply, &n, magic[i]);
    put16(ks->reply, &n, len);
    put16(ks->reply, &n, status);
    if (len)
        memcpy(ks->reply + n, data, len);
    const uint8_t *crc_from = ks->reply + 8;
    n += len;
    uint32_t crc = crc32be(0, crc_from, 4 + len);
    while (n & 3)
        ks->reply[n++] = 0;
    put32(ks->reply, &n, crc);
    ks->reply_len = (uint16_t)n;
    ks->reply_pos = 0;
}

// The identification the switcher shows (smash_id_t, 88 bytes).
static unsigned id_data(uint8_t *p) {
    unsigned n = 0;
    put16(p, &n, 2); put16(p, &n, 0);                 // version 2.0
    p[n++] = 20; p[n++] = 26; p[n++] = 10; p[n++] = 24;  // date
    p[n++] = 12; p[n++] = 0; p[n++] = 0; p[n++] = 0;     // time
    memset(p + n, 0, 24); memcpy(p + n, "OMEGA-RP2350", 12); n += 24;
    put16(p, &n, 1);                                   // features
    put16(p, &n, 1);                                   // protocol 0.1
    put32(p, &n, 0x12091610);                          // USB id
    memset(p + n, 0, 16); memcpy(p + n, ks->board_name, 15); n += 16;
    p[n++] = ks->shift == 1;                           // 16-bit ROM mode
    p[n++] = 0; put16(p, &n, 0);
    memset(p + n, 0, 24); n += 24;
    return n;
}

static void command(void) {
    const uint16_t len = ks->req_len, cmd = ks->req_cmd;
    uint8_t arg[300] = { 0 };
    const unsigned words = (len + 1u) / 2u;
    if (len > sizeof arg || 2u + words + (words & 1u) + 2u > 160u) {
        reply(KS_STATUS_BADLEN, NULL, 0);
        return;
    }
    for (unsigned i = 0; i < words; i++) {
        arg[2 * i] = (uint8_t)(ks->req[2 + i] >> 8);
        arg[2 * i + 1] = (uint8_t)ks->req[2 + i];
    }
    const unsigned at = 2u + words + (words & 1u);
    const uint32_t sent = (uint32_t)ks->req[at] << 16 | ks->req[at + 1];
    uint8_t head[4] = { len >> 8, len, cmd >> 8, cmd };
    if (crc32be(crc32be(0, head, 4), arg, len) != sent) {
        reply(KS_STATUS_CRC, NULL, 0);
        return;
    }
    const unsigned bank = len >= 2 ? (unsigned)arg[0] << 8 | arg[1] : 0;
    uint8_t data[96];

    switch (cmd & 0xff) {
    case KS_CMD_NOP:
        reply(KS_STATUS_OK, NULL, 0);
        return;
    case KS_CMD_ID:
        reply(KS_STATUS_OK, data, id_data(data));
        return;
    case KS_CMD_BANK_INFO:
        reply(KS_STATUS_OK, &ks->bi, sizeof ks->bi);
        return;
    case KS_CMD_GET:
        if ((cmd & KS_GET_NV) && len >= 2 &&
            arg[0] + arg[1] <= (int)sizeof ks->nv) {
            const unsigned count = (arg[1] + 3u) & ~3u;
            memset(data, 0, count);
            memcpy(data, ks->nv + arg[0], arg[1]);
            reply(KS_STATUS_OK, data, count);
        } else {
            reply(KS_STATUS_BADARG, NULL, 0);
        }
        return;
    case KS_CMD_SET:
        if (cmd & KS_SET_NAME) {
            if (len != 16) {
                reply(KS_STATUS_BADLEN, NULL, 0);
                return;
            }
            memcpy(ks->board_name, arg, 16);
            ks->board_name[15] = '\0';
        } else if ((cmd & KS_SET_NV) && len >= 2 &&
                   arg[0] + arg[1] <= (int)sizeof ks->nv &&
                   arg[1] <= len - 2) {
            memcpy(ks->nv + arg[0], arg + 2, arg[1]);
        } else {
            reply(KS_STATUS_BADARG, NULL, 0);
            return;
        }
        reply(KS_STATUS_OK, NULL, 0);
        kickSmashHostSettingsChanged();
        return;
    case KS_CMD_BANK_SET:
        if (len != 2) {
            reply(KS_STATUS_BADLEN, NULL, 0);
            return;
        }
        if (bank >= KS_ROM_BANKS ||
            ((cmd & (KS_BANK_SETCURRENT | KS_BANK_SETRESET)) &&
             !ks->file[bank][0])) {
            reply(KS_STATUS_BADARG, NULL, 0);
            return;
        }
        reply(KS_STATUS_OK, NULL, 0);
        // A bank changes the ROM under the running Amiga only through a
        // reset here: without KS_BANK_REBOOT it waits for the next one.
        if (cmd & (KS_BANK_SETCURRENT | KS_BANK_SETRESET))
            ks->bi.bank_nextreset = (uint8_t)bank;
        if (cmd & KS_BANK_REBOOT)
            switch_bank = ks->bi.bank_nextreset != 0xff
                        ? ks->bi.bank_nextreset : ks->bi.bank_current;
        if (cmd & KS_BANK_SETPOWERON) {
            ks->bi.bank_poweron = (uint8_t)bank;
            kickSmashHostSettingsChanged();
        }
        return;
    case KS_CMD_BANK_MERGE: {
        if (len != 2) {
            reply(KS_STATUS_BADLEN, NULL, 0);
            return;
        }
        const unsigned start = bank & 0xff, end = bank >> 8;
        const unsigned add = end - start;
        if (start > end || end >= KS_ROM_BANKS ||
            (add != 0 && add != 1 && add != 3 && add != 7) ||
            (add == 1 && (start & 1)) ||
            (add == 3 && start != 0 && start != 4) ||
            (add == 7 && start != 0)) {
            reply(KS_STATUS_BADARG, NULL, 0);
            return;
        }
        // Merging takes unmerged banks, unmerging merged ones.
        for (unsigned b = start; b <= end; b++) {
            if ((cmd & KS_BANK_UNMERGE) ? ks->bi.merge[b] != 0
                                        : ks->bi.merge[b] == 0)
                continue;
            reply(KS_STATUS_FAIL, NULL, 0);
            return;
        }
        for (unsigned b = start; b <= end; b++)
            ks->bi.merge[b] = (cmd & KS_BANK_UNMERGE)
                            ? 0 : (uint8_t)(add << 4 | (b - start));
        reply(KS_STATUS_OK, NULL, 0);
        kickSmashHostSettingsChanged();
        return;
    }
    case KS_CMD_BANK_NAME:
        if (len < 2 || bank >= KS_ROM_BANKS) {
            reply(KS_STATUS_BADARG, NULL, 0);
            return;
        }
        if (len - 2u > sizeof ks->bi.name[0]) {
            reply(KS_STATUS_BADLEN, NULL, 0);
            return;
        }
        memset(ks->bi.name[bank], 0, sizeof ks->bi.name[0]);
        memcpy(ks->bi.name[bank], arg + 2, len - 2u);
        ks->bi.name[bank][15] = '\0';
        reply(KS_STATUS_OK, NULL, 0);
        kickSmashHostSettingsChanged();
        return;
    case KS_CMD_BANK_LRESET:
        if (len != KS_ROM_BANKS) {
            reply(KS_STATUS_BADLEN, NULL, 0);
            return;
        }
        for (unsigned b = 0; b < KS_ROM_BANKS; b++)
            if (arg[b] != 0xff && arg[b] >= KS_ROM_BANKS) {
                reply(KS_STATUS_BADARG, NULL, 0);
                return;
            }
        memcpy(ks->bi.longreset_seq, arg, KS_ROM_BANKS);
        reply(KS_STATUS_OK, NULL, 0);
        kickSmashHostSettingsChanged();
        return;
    default:
        reply(KS_STATUS_UNKCMD, NULL, 0);
        return;
    }
}

static void request_reset(void) {
    ks->req_pos = ks->req_need = 0;
    kickSmashBusy = 0;
}

static uint32_t reply_long(void) {
    const uint8_t *p = ks->reply + ks->reply_pos;
    ks->reply_pos += 4;
    if (ks->reply_pos >= ks->reply_len) {
        kickSmashBusy = 0;
        switch_ready = switch_bank;     // the Amiga has its reply
        switch_bank = -1;
    }
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

int kickSmashReadLong(uint32_t address, uint32_t *value) {
    if (!ks || address < KS_ROM_BASE || address >= KS_ROM_BASE + KS_ROM_WINDOW)
        return 0;
    if (m68k_get_reg(NULL, M68K_REG_PC) >= KS_ROM_BASE)
        return 0;                       // the ROM's own code
    const uint32_t off = address - KS_ROM_BASE;

    if (ks->reply_pos < ks->reply_len) {
        *value = reply_long();
        return 1;
    }
    // A complete request waits for the read that starts the reply.
    if (ks->req_need && ks->req_pos >= ks->req_need) {
        if (off != KS_REPLY_TRIGGER)
            return 0;
        command();
        ks->req_pos = ks->req_need = 0;
        *value = reply_long();
        kickSmashBusy = ks->reply_pos < ks->reply_len;
        return 1;
    }
    if (ks->req_pos == 0) {
        if (off == (uint32_t)KS_MAGIC0 << 2)
            ks->shift = 2;
        else if (off == (uint32_t)KS_MAGIC0 << 1)
            ks->shift = 1;
        else
            return 0;
        ks->req_pos = 1;
        kickSmashBusy = 1;
        return 0;
    }
    if (off & ((1u << ks->shift) - 1u)) {
        request_reset();
        return 0;
    }
    const uint16_t w = (uint16_t)(off >> ks->shift);
    if (ks->req_pos < 4) {
        if (w == magic[ks->req_pos])
            ks->req_pos++;
        else if (w == KS_MAGIC0)
            ks->req_pos = 1;
        else
            request_reset();
        return 0;
    }
    const unsigned i = ks->req_pos - 4u;
    if (i < sizeof ks->req / sizeof ks->req[0])
        ks->req[i] = w;
    if (i == 0)
        ks->req_len = w;
    if (i == 1) {
        ks->req_cmd = w;
        const unsigned words = (ks->req_len + 1u) / 2u;
        ks->req_need = (uint16_t)(4u + 2u + words + (words & 1u) + 2u);
    }
    ks->req_pos++;
    if (ks->req_pos > 4u + 160u + 4u)
        request_reset();                // longer than any request
    return 0;
}

// Hosts without settings to keep (the board without SD card) need nothing.
__attribute__((weak)) void kickSmashHostSettingsChanged(void) {}

int kickSmashTakeSwitch(void) {
    const int bank = switch_ready;
    switch_ready = -1;
    return bank;
}

int kickSmashTakeResetBank(void) {
    if (!ks || ks->bi.bank_nextreset == 0xff)
        return -1;
    const int bank = ks->bi.bank_nextreset;
    ks->bi.bank_nextreset = 0xff;
    return bank;
}

static int same_name(const char *a, const char *b) {
    for (;; a++, b++) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y)
            return 0;
        if (!x)
            return 1;
    }
}

int kickSmashFindBank(const char *file) {
    if (!ks || !file || !*file)
        return -1;
    for (int b = 0; b < KS_ROM_BANKS; b++)
        if (same_name(ks->file[b], file))
            return b;
    return -1;
}

// Copies at most size - 1 characters and terminates.
static void copy_str(char *dst, size_t size, const char *src) {
    size_t n = strlen(src);
    if (n > size - 1)
        n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int kickSmashBankCpu020(int bank) {
    return ks && bank >= 0 && bank < KS_ROM_BANKS && ks->cpu020[bank];
}

int kickSmashBankRtg(int bank) {
    return ks && bank >= 0 && bank < KS_ROM_BANKS && ks->rtg[bank];
}

static void set_bank(unsigned b, const char *file, const char *name) {
    copy_str(ks->file[b], KS_FILE_MAX, file);
    memset(ks->bi.name[b], 0, sizeof ks->bi.name[b]);
    if (name && *name) {
        copy_str(ks->bi.name[b], sizeof ks->bi.name[b], name);
    } else {
        // The file name without its extension.
        unsigned n = 0;
        while (file[n] && file[n] != '.' && n < sizeof ks->bi.name[b] - 1) {
            ks->bi.name[b][n] = file[n];
            n++;
        }
    }
}

void kickSmashDefaultBanks(const char *const *files, int count) {
    for (int b = 0; b < KS_ROM_BANKS; b++)
        set_bank((unsigned)b, "", "");
    for (int b = 0; b < count && b < KS_ROM_BANKS; b++)
        set_bank((unsigned)b, files[b], NULL);
}

// ── Settings text ─────────────────────────────────────────────────────────
//  # comment
//  name <board name>
//  poweron <bank>
//  longreset <bank> ...       (up to 8; the rest end the list)
//  merge <8 hex bytes>
//  nv <up to 32 hex bytes>
//  bank <n> <file> [<name>]   (any bank line replaces the default banks)

static const char *skip_space(const char *p) {
    while (*p == ' ' || *p == '\t')
        p++;
    return p;
}

static const char *word(const char *p, char *out, int size) {
    int n = 0;
    p = skip_space(p);
    while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
        if (n < size - 1)
            out[n++] = *p;
        p++;
    }
    out[n] = '\0';
    return p;
}

static int number(const char *s, int base) {
    int v = 0, any = 0;
    for (; *s; s++) {
        int d = *s >= '0' && *s <= '9' ? *s - '0'
              : *s >= 'a' && *s <= 'f' ? *s - 'a' + 10
              : *s >= 'A' && *s <= 'F' ? *s - 'A' + 10 : 99;
        if (d >= base)
            return -1;
        v = v * base + d;
        any = 1;
    }
    return any ? v : -1;
}

void kickSmashParseConfig(const char *text) {
    int banks_given = 0;
    while (*text) {
        const char *line = skip_space(text), *end = line;
        while (*end && *end != '\n')
            end++;
        text = *end ? end + 1 : end;
        if (*line == '#' || line == end)
            continue;
        char key[12], tok[KS_FILE_MAX];
        const char *p = word(line, key, sizeof key);
        if (!strcmp(key, "name") || !strcmp(key, "bank")) {
            int b = -1;
            if (!strcmp(key, "bank")) {
                p = word(p, tok, sizeof tok);
                b = number(tok, 10);
                if (b < 0 || b >= KS_ROM_BANKS)
                    continue;
                if (!banks_given++)
                    kickSmashDefaultBanks(NULL, 0);
                p = word(p, tok, sizeof tok);
            }
            // The rest of the line (trailing blanks and CR dropped).
            p = skip_space(p);
            char rest[16];
            int n = 0;
            while (p < end && n < 15)
                rest[n++] = *p++;
            while (n && (rest[n - 1] == ' ' || rest[n - 1] == '\r' ||
                         rest[n - 1] == '\t'))
                n--;
            rest[n] = '\0';
            if (b < 0)
                strcpy(ks->board_name, rest);
            else
                set_bank((unsigned)b, tok, rest);
        } else if (!strcmp(key, "cpu")) {
            // "cpu <bank> 68000|68020"
            p = word(p, tok, sizeof tok);
            const int b = number(tok, 10);
            word(p, tok, sizeof tok);
            if (b >= 0 && b < KS_ROM_BANKS)
                ks->cpu020[b] = !strcmp(tok, "68020") ||
                                same_name(tok, "68EC020");
        } else if (!strcmp(key, "rtg")) {
            // "rtg <bank> on|off"
            p = word(p, tok, sizeof tok);
            const int b = number(tok, 10);
            word(p, tok, sizeof tok);
            if (b >= 0 && b < KS_ROM_BANKS)
                ks->rtg[b] = same_name(tok, "on");
        } else if (!strcmp(key, "poweron")) {
            word(p, tok, sizeof tok);
            const int b = number(tok, 10);
            if (b >= 0 && b < KS_ROM_BANKS)
                ks->bi.bank_poweron = (uint8_t)b;
        } else if (!strcmp(key, "longreset") || !strcmp(key, "merge") ||
                   !strcmp(key, "nv")) {
            const int nv = !strcmp(key, "nv"), merge = !strcmp(key, "merge");
            uint8_t *dst = nv ? ks->nv : merge ? ks->bi.merge
                                               : ks->bi.longreset_seq;
            const int count = nv ? (int)sizeof ks->nv : KS_ROM_BANKS;
            if (!nv && !merge)
                memset(dst, 0xff, KS_ROM_BANKS);
            for (int i = 0; i < count && p < end; i++) {
                p = word(p, tok, sizeof tok);
                const int v = number(tok, nv || merge ? 16 : 10);
                if (v < 0)
                    break;
                dst[i] = (uint8_t)v;
            }
        }
    }
}

int kickSmashFormatConfig(char *out, int size) {
    int n = snprintf(out, size,
        "# KickSmash ROM switcher settings (Omega). Edited by the switcher's\n"
        "# Save; \"bank <n> <file in /rom> [name]\" lines map banks to files,\n"
        "# \"cpu <n> 68020\" runs a bank on a 68EC020 (else a 68000).\n"
        "# \"rtg <n> on\" gives a bank the Picasso II+ RTG card (RTG builds;\n"
        "# its drivers need a 68020).\n"
        "name %s\npoweron %u\nlongreset",
        ks->board_name, ks->bi.bank_poweron);
    for (int b = 0; b < KS_ROM_BANKS && ks->bi.longreset_seq[b] != 0xff &&
                    n < size; b++)
        n += snprintf(out + n, size - n, " %u", ks->bi.longreset_seq[b]);
    if (n < size)
        n += snprintf(out + n, size - n, "\nmerge");
    for (int b = 0; b < KS_ROM_BANKS && n < size; b++)
        n += snprintf(out + n, size - n, " %02x", ks->bi.merge[b]);
    if (n < size)
        n += snprintf(out + n, size - n, "\nnv");
    for (unsigned i = 0; i < sizeof ks->nv && n < size; i++)
        n += snprintf(out + n, size - n, " %02x", ks->nv[i]);
    if (n < size)
        n += snprintf(out + n, size - n, "\n");
    for (int b = 0; b < KS_ROM_BANKS && n < size; b++)
        if (ks->file[b][0])
            n += snprintf(out + n, size - n, "bank %d %s %s\n", b,
                          ks->file[b], ks->bi.name[b]);
    for (int b = 0; b < KS_ROM_BANKS && n < size; b++)
        if (ks->cpu020[b])
            n += snprintf(out + n, size - n, "cpu %d 68020\n", b);
    for (int b = 0; b < KS_ROM_BANKS && n < size; b++)
        if (ks->rtg[b])
            n += snprintf(out + n, size - n, "rtg %d on\n", b);
    return n < size ? n : -1;
}
