/* run.h — programs: found by name, checked, flashed if they differ, and
 * started by a reboot through stage3; and `menu` (design.md §3.3, §4.3).
 *
 * Portable. What stage3 is told, and how, is the port's (loader.c); the
 * core sees the app partition as a uf2_flash_t and a boot hook. */
#ifndef SHELL_RUN_H
#define SHELL_RUN_H

#include <stdbool.h>

#include "shell.h"
#include "uf2.h"

/* $PATH's default: where programs are looked for (design.md §4.3). */
#define SHELL_PROGRAM_DIR "/pico2-apps"
#define SHELL_PROGRAM_EXT ".uf2"
/* The loader's own menu, kept beside the shell (design.md §3.2). */
#define SHELL_MENU_UF2 "/BOOT2350.ORG"

typedef struct shell_loader {
    /* NULL when programs can be run; otherwise why not, as "no UF2
     * Loader partition table". */
    const char *problem;
    uf2_flash_t flash;
    /* Reboot through stage3: with `ram` NULL it starts the program in
     * flash; otherwise it loads the RAM program at that path, every
     * name in it 8.3 (stage3 has no long names). Never returns on the
     * board. `info` is the run's, for the development build's log. */
    void (*boot)(void *ctx, const char *ram, const uf2_info_t *info);
    void *ctx;
} shell_loader_t;

/* A command that is not a builtin: a program by name or path. Prints
 * "not found", status 127, if there is none. */
int run_program(shell_t *sh, int argc, char **argv, bool redirected);

int cmd_menu(shell_t *sh, int argc, char **argv);

#endif /* SHELL_RUN_H */
