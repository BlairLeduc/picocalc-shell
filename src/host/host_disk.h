/* host_disk.h — FatFs's disk on the host: a FAT image file
 * (design.md §6).
 *
 * The host's counterpart of src/port/diskio.c, for the tests and the host
 * shell. With no image open, the disk reads as no card. */
#ifndef SHELL_HOST_DISK_H
#define SHELL_HOST_DISK_H

#include <stdbool.h>
#include <stdint.h>

/* Use an existing image. False if it cannot be opened. */
bool host_disk_open(const char *path);

/* A new image of `bytes`, formatted by FatFs as FAT16 or FAT32 (fat32),
 * at `path`, or in a temporary file that goes when closed if path is
 * NULL. It is left open. False if it cannot be made. */
bool host_disk_create(const char *path, uint64_t bytes, bool fat32);

void host_disk_close(void);

/* Fail every write after the next `n` sectors, as a card that dies or is
 * pulled would; -1 for never. */
void host_disk_fail_writes_after(long n);

/* Sectors written since the image was opened. */
unsigned long host_disk_writes(void);

#endif /* SHELL_HOST_DISK_H */
