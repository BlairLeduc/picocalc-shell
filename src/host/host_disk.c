/* host_disk.c — FatFs's disk layer over an image file (host_disk.h). */

#include "host_disk.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"
#include "diskio.h"

#define SECTOR 512u

static FILE         *s_img;
static LBA_t         s_sectors;
static long          s_writes_left = -1;
static unsigned long s_writes;
/* A new image reads as a card just inserted: not initialised, so FatFs
 * mounts it afresh, as it does after the port's card detect changes. */
static bool          s_changed;

static bool attach(FILE *f) {
    if (fseek(f, 0, SEEK_END) != 0) return false;
    long size = ftell(f);
    if (size < 0) return false;
    s_img = f;
    s_sectors = (LBA_t)((unsigned long)size / SECTOR);
    s_writes = 0;
    s_writes_left = -1;
    s_changed = true;
    return true;
}

bool host_disk_open(const char *path) {
    host_disk_close();
    FILE *f = fopen(path, "r+b");
    if (!f) return false;
    if (!attach(f)) {
        fclose(f);
        return false;
    }
    return true;
}

bool host_disk_create(const char *path, uint64_t bytes, bool fat32) {
    host_disk_close();
    FILE *f = path ? fopen(path, "w+b") : tmpfile();
    if (!f) return false;
    /* A sparse file of the size: the last byte written. */
    if (fseek(f, (long)(bytes - 1u), SEEK_SET) != 0 || fputc(0, f) == EOF || !attach(f)) {
        fclose(f);
        return false;
    }
    static BYTE work[FF_MAX_SS];
    /* FAT32 wants at least 65,525 clusters, so small clusters on a small
     * image; FAT16 takes FatFs's choice. */
    MKFS_PARM opt = { (BYTE)((fat32 ? FM_FAT32 : FM_FAT) | FM_SFD), 2, 0, 0,
                      fat32 ? SECTOR : 0 };
    if (f_mkfs("", &opt, work, sizeof work) != FR_OK) {
        host_disk_close();
        return false;
    }
    s_writes = 0;
    return true;
}

void host_disk_close(void) {
    if (s_img) fclose(s_img);
    s_img = NULL;
    s_sectors = 0;
    s_changed = true;
}

void host_disk_fail_writes_after(long n) {
    s_writes_left = n;
}

unsigned long host_disk_writes(void) {
    return s_writes;
}

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0 || !s_img) return STA_NOINIT | STA_NODISK;
    return s_changed ? STA_NOINIT : 0;
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv == 0 && s_img) s_changed = false;
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (!s_img) return RES_NOTRDY;
    if (sector + count > s_sectors) return RES_PARERR;
    if (fseek(s_img, (long)(sector * SECTOR), SEEK_SET) != 0) return RES_ERROR;
    return fread(buff, SECTOR, count, s_img) == count ? RES_OK : RES_ERROR;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0 || count == 0) return RES_PARERR;
    if (!s_img) return RES_NOTRDY;
    if (sector + count > s_sectors) return RES_PARERR;
    for (UINT i = 0; i < count; i++) {
        /* As on the board (src/port/diskio.c), a failed transfer leaves
         * the disk to be initialised and mounted afresh. */
        if (s_writes_left == 0) {
            s_changed = true;
            return RES_ERROR;
        }
        if (s_writes_left > 0) s_writes_left--;
        if (fseek(s_img, (long)((sector + i) * SECTOR), SEEK_SET) != 0 ||
            fwrite(buff + i * SECTOR, SECTOR, 1, s_img) != 1)
            return RES_ERROR;
        s_writes++;
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) return RES_PARERR;
    if (!s_img) return RES_NOTRDY;
    switch (cmd) {
    case CTRL_SYNC:
        return fflush(s_img) == 0 ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        *(LBA_t *)buff = s_sectors;
        return RES_OK;
    case GET_BLOCK_SIZE:
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
