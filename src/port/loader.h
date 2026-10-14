/* loader.h — the shell's link to UF2 Loader (design.md §3, §8).
 *
 * All knowledge of the loader's private protocol lives in loader.c: the
 * flash end stage3 leaves in the shell's vector table, the app partition
 * and its remapping to 0x10000000, stage3's version, the scratch
 * commands, and erasing and programming the partition. */
#ifndef SHELL_LOADER_H
#define SHELL_LOADER_H

#include <stdint.h>

#include "run.h"

typedef struct {
    const char *version;      /* stage3's, or NULL if it has none we can read */
    uint32_t    part_offset;  /* the app partition in flash, 0 if none */
    uint32_t    part_size;
    uint32_t    flash_end;    /* as stage3 left it, 0 if it did not */
} loader_status_t;

/* Find the loader, and map the app partition at 0x10000000 if there is
 * one, as the loader's menu does. Fills `ld`, whose problem says why
 * programs cannot be run if they cannot. Call once, before anything
 * reads flash. */
void loader_init(shell_loader_t *ld, loader_status_t *st);

/* Reboot, telling stage3 to load the shell from the card (BOOT_SD), as
 * picocalc_shell_exit() does. The caller checks there is a card and a
 * loader in flash: without them stage3 would drop to BOOTSEL. Does not
 * return. */
void loader_reboot_shell(void);

#endif /* SHELL_LOADER_H */
