//
//  Gayle.c
//  Omega
//
//  Created by Matt Parsons on 06/03/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//

#include "Gayle.h"

#define CLOCKBASE 0xDC0000

// The battery clock is not emulated: long reads give two fixed values.
static uint32_t readClockL(unsigned int address) {
    static int count = 0;

    if (address >= CLOCKBASE) {
        if (count == 0) {
            count += 1;
            return 1309036038;
        }

        return 1309101575;
    }

    return 0x8000;
}

#if !OMEGA_ENABLE_IDE

void writeGayleB(unsigned int address, unsigned int value){
    (void)address;
    (void)value;
}

void writeGayle(unsigned int address, unsigned int value){
    (void)address;
    (void)value;
}

void writeGayleL(unsigned int address, unsigned int value){
    (void)address;
    (void)value;
}

uint8_t readGayleB(unsigned int address){
    (void)address;
    return 0xFF;
}

uint16_t readGayle(unsigned int address){
    (void)address;
    return 0x8000;
}

uint32_t readGayleL(unsigned int address){
    return readClockL(address);
}

int gayleInterruptPending(void) {
    return 0;
}

#else

#include <string.h>

void intreq(uint16_t value);     // Chipset.c

// ── Gayle (A600) ─────────────────────────────────────────────────────────
// 0xDA0000-0xDA7FFF  IDE: register (address >> 2) & 7 in the high byte of
//                    the word (byte at the even address); 0xDA2000 is the
//                    16-bit data register, 0xDA3000+ the control block
//                    (alternate status / device control at 0xDA3018)
// 0xDA8000  status   bit 7: the IDE interrupt line
// 0xDA9000  IRQ      bit 7: IDE interrupt seen (latched; written 0 clears)
// 0xDAA000  INTENA   bit 7: IDE interrupt raises INT2
// 0xDAB000  config
// 0xDE1000  ID       each read returns the next bit of 0xD0 (A600) in
//                    bit 7, most significant first; a write restarts it
#define GAYLE_ID       0xD0u
#define GAYLE_IRQ_IDE  0x80u

static uint8_t gayleStatus, gayleIrq, gayleIntena, gayleConfig;
static unsigned gayleIdBit;

// ── ATA drive (master; no slave) ─────────────────────────────────────────
#define ATA_SR_BSY   0x80u
#define ATA_SR_DRDY  0x40u
#define ATA_SR_DSC   0x10u
#define ATA_SR_DRQ   0x08u
#define ATA_SR_ERR   0x01u
#define ATA_ER_ABRT  0x04u
#define ATA_ER_IDNF  0x10u
#define ATA_DH_SLAVE 0x10u
#define ATA_DH_LBA   0x40u
#define ATA_DC_NIEN  0x02u
#define ATA_DC_SRST  0x04u

#define ATA_HEADS    16u        // default geometry: the test images match it
#define ATA_SPT      63u
#define ATA_MULTIPLE_MAX 16u
#define ATA_SECTOR_BYTES 512u

enum { XFER_NONE, XFER_READ, XFER_WRITE };

static struct {
    uint8_t error, feature, nsector, sector, lcyl, hcyl, select;
    uint8_t status, control;
    int irq;                    // INTRQ (before nIEN)
    int heads, spt;             // translation for CHS addressing
    unsigned multiple;          // READ/WRITE MULTIPLE block, 0: off
    int xfer;                   // data phase in progress
    uint32_t lba;               // sector in the buffer
    unsigned left;              // sectors left in the command
    unsigned block;             // sectors per interrupt (DRQ block)
    unsigned inBlock;           // sectors left in this block
    unsigned pos;               // byte position in the buffer
    int identify;               // the buffer holds IDENTIFY data
    uint8_t *buffer;            // one sector, from gayleDiskBuffer()
} ata = { .status = ATA_SR_DRDY | ATA_SR_DSC, .heads = ATA_HEADS,
          .spt = ATA_SPT };

static int gayleIdeLine(void) {
    return ata.irq && !(ata.control & ATA_DC_NIEN);
}

int gayleInterruptPending(void) {
    return (gayleIrq & gayleIntena & GAYLE_IRQ_IDE) != 0;
}

// The IDE line latches Gayle's IRQ bit on its rising edge; an enabled
// pending bit holds INT2.
static void gayleUpdateInterrupt(void) {
    static int line;
    const int now = gayleIdeLine();
    if (now && !line)
        gayleIrq |= GAYLE_IRQ_IDE;
    line = now;
    if (gayleInterruptPending())
        intreq(0x8008);
}

static void ataInterrupt(void) {
    ata.irq = 1;
    gayleUpdateInterrupt();
}

static int ataSlave(void) {
    return (ata.select & ATA_DH_SLAVE) != 0;
}

static uint32_t ataSectors(void) {
    return gayleDiskSectors();
}

static void ataCylinders(uint32_t sectors, unsigned *cyl) {
    uint32_t c = sectors / (ATA_HEADS * ATA_SPT);
    *cyl = c > 16383u ? 16383u : c;
}

static uint32_t ataTaskLba(void) {
    if (ata.select & ATA_DH_LBA)
        return (uint32_t)(ata.select & 0x0f) << 24 |
               (uint32_t)ata.hcyl << 16 | (uint32_t)ata.lcyl << 8 |
               ata.sector;
    const uint32_t cyl = (uint32_t)ata.hcyl << 8 | ata.lcyl;
    const uint32_t head = ata.select & 0x0f;
    return (cyl * (uint32_t)ata.heads + head) * (uint32_t)ata.spt +
           (ata.sector ? ata.sector - 1u : 0u);
}

// The task file shows the last sector transferred.
static void ataSetTaskLba(uint32_t lba) {
    if (ata.select & ATA_DH_LBA) {
        ata.sector = (uint8_t)lba;
        ata.lcyl = (uint8_t)(lba >> 8);
        ata.hcyl = (uint8_t)(lba >> 16);
        ata.select = (uint8_t)((ata.select & 0xf0) | ((lba >> 24) & 0x0f));
        return;
    }
    const uint32_t track = lba / (uint32_t)ata.spt;
    const uint32_t cyl = track / (uint32_t)ata.heads;
    ata.sector = (uint8_t)(lba % (uint32_t)ata.spt + 1u);
    ata.lcyl = (uint8_t)cyl;
    ata.hcyl = (uint8_t)(cyl >> 8);
    ata.select = (uint8_t)((ata.select & 0xf0) |
                           (track % (uint32_t)ata.heads));
}

static void ataDone(void) {
    ata.xfer = XFER_NONE;
    ata.status = ATA_SR_DRDY | ATA_SR_DSC;
    ataInterrupt();
}

static void ataAbort(uint8_t error) {
    ata.xfer = XFER_NONE;
    ata.error = error;
    ata.status = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_ERR;
    ataInterrupt();
}

// ATA strings: two characters a word, the first in the high byte.
static void ataString(uint8_t *words, unsigned count, const char *text) {
    for (unsigned i = 0; i < count * 2u; ++i) {
        const char c = *text ? *text++ : ' ';
        words[i ^ 1u] = (uint8_t)c;
    }
}

static void ataWord(unsigned index, unsigned value) {
    ata.buffer[2u * index] = (uint8_t)value;          // little endian, as
    ata.buffer[2u * index + 1u] = (uint8_t)(value >> 8);  // on the cable
}

static void ataIdentify(void) {
    const uint32_t sectors = ataSectors();
    unsigned cyl;
    ataCylinders(sectors, &cyl);
    const uint32_t cur_chs = (uint32_t)cyl * (uint32_t)ata.heads *
                             (uint32_t)ata.spt;
    const uint32_t lba = sectors > 0x0fffffffu ? 0x0fffffffu : sectors;
    memset(ata.buffer, 0, ATA_SECTOR_BYTES);
    ataWord(0, 0x0040);                 // fixed disk
    ataWord(1, cyl);
    ataWord(3, ATA_HEADS);
    ataWord(6, ATA_SPT);
    ataString(&ata.buffer[20], 10, "OMEGA00000001");
    ataString(&ata.buffer[46], 4, "1.0");
    ataString(&ata.buffer[54], 20, "OMEGA HDF");
    ataWord(47, 0x8000 | ATA_MULTIPLE_MAX);
    ataWord(49, 0x0200);                // LBA
    ataWord(51, 0x0200);                // PIO mode 2
    ataWord(53, 0x0001);                // words 54-58 valid
    ataWord(54, cyl);
    ataWord(55, (unsigned)ata.heads);
    ataWord(56, (unsigned)ata.spt);
    ataWord(57, cur_chs & 0xffff);
    ataWord(58, cur_chs >> 16);
    ataWord(59, ata.multiple ? 0x0100 | ata.multiple : 0);
    ataWord(60, lba & 0xffff);
    ataWord(61, lba >> 16);
}

// Fills the buffer with the next sector of a read; 0 on a failed read.
static int ataLoadSector(void) {
    if (ata.lba >= ataSectors() || gayleDiskRead(ata.lba, ata.buffer)) {
        ataAbort(ATA_ER_IDNF);
        return 0;
    }
    ata.pos = 0;
    return 1;
}

// Starts a DRQ block: the first sector of a read is loaded, a write waits
// for its data. A read interrupts at each block, a write at each block
// after the first (the host writes the first without waiting).
static void ataStartBlock(int interrupt) {
    ata.inBlock = ata.left < ata.block ? ata.left : ata.block;
    if (ata.xfer == XFER_READ && !ataLoadSector())
        return;
    ata.pos = 0;
    ata.status = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_DRQ;
    if (interrupt)
        ataInterrupt();
}

static void ataStartTransfer(int xfer, unsigned block) {
    ata.xfer = xfer;
    ata.left = ata.nsector ? ata.nsector : 256u;
    ata.block = block;
    ata.lba = ataTaskLba();
    if (ata.lba + ata.left > ataSectors()) {
        ataAbort(ATA_ER_IDNF);
        return;
    }
    ataStartBlock(xfer == XFER_READ);
}

// A sector's data has gone across the data register.
static void ataSectorDone(void) {
    if (ata.identify) {                 // the task file keeps its values
        ata.xfer = XFER_NONE;
        ata.status = ATA_SR_DRDY | ATA_SR_DSC;
        return;
    }
    if (ata.xfer == XFER_WRITE && gayleDiskWrite(ata.lba, ata.buffer)) {
        ataAbort(ATA_ER_IDNF);
        return;
    }
    ataSetTaskLba(ata.lba);
    ata.lba++;
    ata.left--;
    ata.inBlock--;
    if (!ata.left) {
        ata.nsector = 0;
        if (ata.xfer == XFER_READ) {
            ata.xfer = XFER_NONE;
            ata.status = ATA_SR_DRDY | ATA_SR_DSC;
        } else {
            ataDone();
        }
        return;
    }
    ata.nsector = (uint8_t)ata.left;
    if (ata.inBlock) {
        if (ata.xfer == XFER_READ)
            ataLoadSector();
        ata.pos = 0;
        return;
    }
    ataStartBlock(1);
}

static void ataCommand(uint8_t command) {
    if (ataSlave())
        return;
    ata.irq = 0;
    ata.error = 0;
    ata.identify = 0;
    if (!ataSectors()) {
        ataAbort(ATA_ER_ABRT);
        return;
    }
    switch (command) {
    case 0xEC:                          // IDENTIFY DEVICE
        ataIdentify();
        ata.xfer = XFER_READ;
        ata.left = ata.block = ata.inBlock = 1;
        ata.lba = 0;
        ata.pos = 0;
        ata.identify = 1;
        ata.status = ATA_SR_DRDY | ATA_SR_DSC | ATA_SR_DRQ;
        ataInterrupt();
        return;
    case 0x20: case 0x21:               // READ SECTOR(S)
        ataStartTransfer(XFER_READ, 1);
        return;
    case 0x30: case 0x31:               // WRITE SECTOR(S)
        ataStartTransfer(XFER_WRITE, 1);
        return;
    case 0xC4:                          // READ MULTIPLE
    case 0xC5:                          // WRITE MULTIPLE
        if (!ata.multiple) {
            ataAbort(ATA_ER_ABRT);
            return;
        }
        ataStartTransfer(command == 0xC4 ? XFER_READ : XFER_WRITE,
                         ata.multiple);
        return;
    case 0xC6:                          // SET MULTIPLE MODE
        if (ata.nsector > ATA_MULTIPLE_MAX ||
            (ata.nsector & (ata.nsector - 1u))) {
            ataAbort(ATA_ER_ABRT);
            return;
        }
        ata.multiple = ata.nsector;
        ataDone();
        return;
    case 0x91:                          // INITIALIZE DEVICE PARAMETERS
        ata.spt = ata.nsector ? ata.nsector : 1;
        ata.heads = (ata.select & 0x0f) + 1;
        ataDone();
        return;
    case 0x90:                          // EXECUTE DEVICE DIAGNOSTIC
        ataDone();
        ata.error = 0x01;               // device 0 passed, no device 1
        return;
    case 0x40: case 0x41:               // READ VERIFY SECTOR(S)
    case 0x70:                          // SEEK
    case 0xE0: case 0xE1: case 0xE2: case 0xE3: // power management
    case 0xE6: case 0xE7:               // SLEEP, FLUSH CACHE
    case 0xEF:                          // SET FEATURES
        ataDone();
        return;
    case 0xE5:                          // CHECK POWER MODE: active
        ata.nsector = 0xFF;
        ataDone();
        return;
    default:
        if ((command & 0xF0) == 0x10) { // RECALIBRATE
            ataDone();
            return;
        }
        ataAbort(ATA_ER_ABRT);
        return;
    }
}

static void ataReset(void) {
    ata.xfer = XFER_NONE;
    ata.error = 0x01;
    ata.nsector = ata.sector = 1;
    ata.lcyl = ata.hcyl = 0;
    ata.select = 0;
    ata.irq = 0;
    ata.status = ATA_SR_DRDY | ATA_SR_DSC;
}

static uint16_t ataReadData(void) {
    if (ata.xfer != XFER_READ || ataSlave())
        return 0xFFFF;
    const uint16_t v = (uint16_t)(ata.buffer[ata.pos] << 8 |
                                  ata.buffer[ata.pos + 1u]);
    ata.pos += 2u;
    if (ata.pos >= ATA_SECTOR_BYTES)
        ataSectorDone();
    return v;
}

static void ataWriteData(uint16_t value) {
    if (ata.xfer != XFER_WRITE || ataSlave())
        return;
    ata.buffer[ata.pos] = (uint8_t)(value >> 8);
    ata.buffer[ata.pos + 1u] = (uint8_t)value;
    ata.pos += 2u;
    if (ata.pos >= ATA_SECTOR_BYTES)
        ataSectorDone();
}

static uint8_t ataReadRegister(int control, unsigned reg) {
    if (control) {
        if (reg == 6)                   // alternate status
            return ataSlave() ? 0 : ata.status;
        return 0xFF;
    }
    if (ataSlave() && reg != 6)
        return 0;                       // no device 1
    switch (reg) {
    case 1: return ata.error;
    case 2: return ata.nsector;
    case 3: return ata.sector;
    case 4: return ata.lcyl;
    case 5: return ata.hcyl;
    case 6: return ata.select | 0xA0;
    case 7:
        ata.irq = 0;                    // reading status acknowledges
        gayleUpdateInterrupt();
        return ata.status;
    default: return 0xFF;
    }
}

static void ataWriteRegister(int control, unsigned reg, uint8_t value) {
    if (control) {
        if (reg != 6)
            return;
        const uint8_t old = ata.control;
        ata.control = value;
        if ((old & ATA_DC_SRST) && !(value & ATA_DC_SRST))
            ataReset();
        else if (value & ATA_DC_SRST)
            ata.status = ATA_SR_BSY;
        gayleUpdateInterrupt();
        return;
    }
    switch (reg) {
    case 1: ata.feature = value; break;
    case 2: ata.nsector = value; break;
    case 3: ata.sector = value; break;
    case 4: ata.lcyl = value; break;
    case 5: ata.hcyl = value; break;
    case 6: ata.select = value; break;
    case 7: ataCommand(value); break;
    }
}

// ── Address decoding ─────────────────────────────────────────────────────
static int ideRegister(unsigned int address, int *control, unsigned *reg) {
    if ((address & 0xFF8000u) != 0xDA0000u)
        return 0;
    *control = (address & 0x1000u) != 0;
    *reg = (address >> 2) & 7u;
    return 1;
}

static int isDataRegister(unsigned int address) {
    int control;
    unsigned reg;
    return ideRegister(address, &control, &reg) && !control && reg == 0;
}

static uint8_t gayleReadByte(unsigned int address) {
    int control;
    unsigned reg;
    if (ideRegister(address, &control, &reg)) {
        if (address & 1u)
            return 0xFF;
        if (!control && reg == 0)
            return (uint8_t)(ataReadData() >> 8);
        return ataReadRegister(control, reg);
    }
    switch (address & 0xFFF000u) {
    case 0xDA8000u:
        return (uint8_t)(gayleStatus | (gayleIdeLine() ? 0x80u : 0u));
    case 0xDA9000u: return gayleIrq;
    case 0xDAA000u: return gayleIntena;
    case 0xDAB000u: return gayleConfig;
    case 0xDE1000u: {
        if ((address & ~1u) != 0xDE1000u)
            return 0xFF;
        const uint8_t bit = (uint8_t)((GAYLE_ID << gayleIdBit) & 0x80u);
        if (gayleIdBit < 8u)
            gayleIdBit++;
        return bit;
    }
    }
    return 0xFF;
}

static void gayleWriteByte(unsigned int address, uint8_t value) {
    int control;
    unsigned reg;
    if (ideRegister(address, &control, &reg)) {
        if (!(address & 1u))
            ataWriteRegister(control, reg, value);
        return;
    }
    switch (address & 0xFFF000u) {
    case 0xDA8000u: gayleStatus = value & 0x7Fu; break;
    case 0xDA9000u:
        gayleIrq = (uint8_t)((gayleIrq & value) | (value & 0x03u));
        break;
    case 0xDAA000u: gayleIntena = value; break;
    case 0xDAB000u: gayleConfig = value; break;
    case 0xDE1000u:
        if ((address & ~1u) == 0xDE1000u)
            gayleIdBit = 0;
        break;
    default: return;
    }
    gayleUpdateInterrupt();
}

// Without a disk image there is no Gayle: the addresses answer as before
// (as do the ones it does not decode), so Kickstart finds no IDE port.
static int gayleDecodes(unsigned int address) {
    if (!ataSectors())
        return 0;
    if (!ata.buffer)
        ata.buffer = gayleDiskBuffer();
    return (address & 0xFF8000u) == 0xDA0000u ||
           (address & 0xFFC000u) == 0xDA8000u ||
           (address & ~1u) == 0xDE1000u;
}

void writeGayleB(unsigned int address, unsigned int value){
    if (gayleDecodes(address))
        gayleWriteByte(address, (uint8_t)value);
}

void writeGayle(unsigned int address, unsigned int value){
    if (!gayleDecodes(address))
        return;
    if (isDataRegister(address)) {
        ataWriteData((uint16_t)value);
        return;
    }
    gayleWriteByte(address, (uint8_t)(value >> 8));
}

void writeGayleL(unsigned int address, unsigned int value){
    writeGayle(address, value >> 16);
    writeGayle(address + 2u, value & 0xFFFFu);
}

uint8_t readGayleB(unsigned int address){
    return gayleDecodes(address) ? gayleReadByte(address) : 0xFF;
}

uint16_t readGayle(unsigned int address){
    if (!gayleDecodes(address))
        return 0x8000;
    if (isDataRegister(address))
        return ataReadData();
    return (uint16_t)(gayleReadByte(address) << 8 | 0xFF);
}

uint32_t readGayleL(unsigned int address){
    if (!gayleDecodes(address))
        return readClockL(address);
    const uint32_t high = readGayle(address);
    return high << 16 | readGayle(address + 2u);
}

#endif
