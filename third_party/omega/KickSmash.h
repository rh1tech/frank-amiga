//
//  KickSmash.h
//  Omega
//
//  The target side of the KickSmash32 ROM-bus message protocol, which the
//  KickSmash ROM switcher (and its AmigaOS tools) use to list and switch
//  Kickstart banks. Requests arrive as 32-bit reads in the Kickstart ROM
//  window, a 16-bit word encoded in each address; the reply is returned by
//  the ROM reads that follow. The host provides the banks (ROM files) and
//  keeps the settings (see the kickSmashHost* functions).
//

#ifndef KickSmash_h
#define KickSmash_h

#include <stdint.h>

#define KS_ROM_BANKS 8
#define KS_FILE_MAX  32          // bank file name, NUL included

// As the protocol's bank_info_t (fw/smash_cmd.h), byte for byte.
typedef struct {
    uint8_t valid;               // 0x01
    uint8_t bank_current;        // bank the Amiga runs
    uint8_t bank_nextreset;      // bank at the next reset, 0xff: current
    uint8_t bank_poweron;        // bank at power-on
    uint8_t longreset_seq[KS_ROM_BANKS];  // 0xff ends the list
    uint8_t merge[KS_ROM_BANKS];
    char    name[KS_ROM_BANKS][16];
    uint8_t unused[12];
} KickSmashBankInfo;

// Everything the protocol keeps. The host owns the memory (PSRAM on the
// board: SRAM is full) and hands it to kickSmashInit().
typedef struct {
    KickSmashBankInfo bi;
    uint8_t  nv[32];             // NV[0]: switcher timeout, NV[1]: its bank
    char     board_name[16];
    char     file[KS_ROM_BANKS][KS_FILE_MAX];  // ROM file of each bank
    uint8_t  cpu020[KS_ROM_BANKS];   // nonzero: the bank runs a 68EC020
    uint8_t  rtg[KS_ROM_BANKS];      // nonzero: the bank has the RTG card
    // Transport
    uint8_t  shift;              // request word = offset >> shift
    uint16_t req_pos;            // words received (magic included)
    uint16_t req_need;           // words the request has, 0: unknown yet
    uint16_t req_len, req_cmd;
    uint16_t req[160];           // the request's words after the magic
    uint16_t reply_len, reply_pos;
    uint8_t  reply[256];
} KickSmashState;

// Nonzero while a request or a reply is in progress (Memory.c tests it on
// every ROM long read, with the first magic word's two addresses).
extern volatile uint8_t kickSmashBusy;
#define KS_ROM_BASE   0xF80000u
#define KS_MAGIC0     0x0204u
static inline int kickSmashWatches(uint32_t address) {
    return kickSmashBusy || address == KS_ROM_BASE + (KS_MAGIC0 << 2) ||
           address == KS_ROM_BASE + (KS_MAGIC0 << 1);
}

void kickSmashInit(KickSmashState *state);
// A 32-bit ROM read the protocol may take: nonzero (and *value) if it did.
int kickSmashReadLong(uint32_t address, uint32_t *value);

// Settings text (the host's kicksmash.cfg): parsed over the state's
// defaults, and written back after the switcher saves.
void kickSmashParseConfig(const char *text);
int kickSmashFormatConfig(char *out, int size);
// Fills banks 0.. from ROM file names (sorted), naming each after its file.
void kickSmashDefaultBanks(const char *const *files, int count);
// The bank whose file is `file` (case-insensitive), or -1.
int kickSmashFindBank(const char *file);
// Nonzero if the bank runs on a 68EC020 ("cpu <n> 68020" in the settings),
// else it runs on a 68000.
int kickSmashBankCpu020(int bank);
// Nonzero if the bank has the RTG card ("rtg <n> on" in the settings; off
// without a line, and for no bank).
int kickSmashBankRtg(int bank);

// Host side. The switch is requested from inside a ROM read: the host
// acts on it between emulation slices (kickSmashTakeSwitch()), loading the
// bank's ROM and resetting the Amiga, as KickSmash's reset line does.
void kickSmashHostSettingsChanged(void);     // save the settings soon
// Bank to switch to and reset into, or -1 (cleared when taken).
int kickSmashTakeSwitch(void);
// The bank to run after an Amiga reset (KS_BANK_SETRESET), or -1.
int kickSmashTakeResetBank(void);
KickSmashState *kickSmashState(void);

#endif /* KickSmash_h */
