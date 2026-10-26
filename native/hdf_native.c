// Native backend of the emulated IDE drive (omega/Gayle.c): the hard-disk
// image named by OMEGA_HDF, a raw RDB image of 512-byte sectors. It is
// opened read-write (read-only if that fails: writes then report an error
// to the Amiga); without OMEGA_HDF there is no drive.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "Gayle.h"

static int hdf_fd = -1;
static uint32_t hdf_sectors;
static int hdf_opened;
static uint8_t hdf_buffer[512];

static void hdf_open(void) {
    hdf_opened = 1;
    const char *path = getenv("OMEGA_HDF");
    if (!path || !*path)
        return;
    hdf_fd = open(path, O_RDWR);
    if (hdf_fd < 0)
        hdf_fd = open(path, O_RDONLY);
    if (hdf_fd < 0) {
        printf("  HDF: cannot open %s\n", path);
        return;
    }
    const off_t size = lseek(hdf_fd, 0, SEEK_END);
    if (size < 512) {
        printf("  HDF: %s is too small\n", path);
        close(hdf_fd);
        hdf_fd = -1;
        return;
    }
    hdf_sectors = (uint32_t)(size / 512);
    printf("  HDF: %s, %u sectors (%u MB)\n", path, hdf_sectors,
           hdf_sectors / 2048u);
}

uint32_t gayleDiskSectors(void) {
    if (!hdf_opened)
        hdf_open();
    return hdf_sectors;
}

uint8_t *gayleDiskBuffer(void) {
    return hdf_buffer;
}

int gayleDiskRead(uint32_t lba, uint8_t *buffer) {
    if (hdf_fd < 0 || lba >= hdf_sectors)
        return -1;
    return pread(hdf_fd, buffer, 512, (off_t)lba * 512) == 512 ? 0 : -1;
}

int gayleDiskWrite(uint32_t lba, const uint8_t *buffer) {
    if (hdf_fd < 0 || lba >= hdf_sectors)
        return -1;
    return pwrite(hdf_fd, buffer, 512, (off_t)lba * 512) == 512 ? 0 : -1;
}
