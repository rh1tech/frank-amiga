//
//  Gayle.h
//  Omega
//
//  Created by Matt Parsons on 06/03/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//

#ifndef Gayle_h
#define Gayle_h

#include <stdio.h>
#include <stdint.h>

// The A600's Gayle with one IDE drive, which Kickstart 2.05 and 3.1.4 boot
// from with their own scsi.device. On in the native runner and in RP2350
// builds with the SD card (src/ide_sd.c); other RP2350 builds keep the old
// stubs.
#ifndef OMEGA_ENABLE_IDE
#ifdef PICO_BUILD
#define OMEGA_ENABLE_IDE OMEGA_ENABLE_SDCARD
#else
#define OMEGA_ENABLE_IDE 1
#endif
#endif

void writeGayleB(unsigned int address, unsigned value);
void writeGayle(unsigned int address, unsigned value);
void writeGayleL(unsigned int address, unsigned value);
uint8_t readGayleB(unsigned int address);
uint16_t readGayle(unsigned int address);
uint32_t readGayleL(unsigned int address);

// Gayle holds INT2 (PORTS) while its IDE interrupt is pending and enabled;
// INTREQ cannot clear it then (see intreq() in Chipset.c).
int gayleInterruptPending(void);

#if OMEGA_ENABLE_IDE
// The drive's disk image, provided by the host: a raw hard-disk image
// (RDB) of 512-byte sectors. gayleDiskSectors() returns 0 for no drive;
// the reads and writes return 0 on success. gayleDiskBuffer() is the
// drive's one-sector (512-byte) buffer, asked for once a drive exists.
uint32_t gayleDiskSectors(void);
uint8_t *gayleDiskBuffer(void);
int gayleDiskRead(uint32_t lba, uint8_t *buffer);
int gayleDiskWrite(uint32_t lba, const uint8_t *buffer);
#endif

#endif /* Gayle_h */
