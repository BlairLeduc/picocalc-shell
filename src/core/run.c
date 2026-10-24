/* run.c — programs and `menu` (run.h). */

#include "run.h"

#include <stdio.h>
#include <string.h>

#include "files.h"
#include "flavour.h"
#include "path.h"
#include "vars.h"

static FIL      s_prog;
static FILINFO  s_fno;
static char     s_path[SHELL_PATH_MAX + 1];
static char     s_sfn[SHELL_PATH_MAX + 1];

/* The path stage3 can open: each name of the absolute `path` as its 8.3
 * entry, as the loader's menu makes it (get_short_path in ui/uf2.c).
 * FatFs gives the 8.3 name as altname, or, for a name that has no long
 * one and no case of its own, leaves altname empty and fname is it. */
static FRESULT short_path(const char *path, char out[SHELL_PATH_MAX + 1]) {
    char prefix[SHELL_PATH_MAX + 1];
    size_t olen = 0;
    const char *p = path;
    while (*p == '/') p++;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t end = slash ? (size_t)(slash - path) : strlen(path);
        memcpy(prefix, path, end);
        prefix[end] = '\0';
        FRESULT r = f_stat(prefix, &s_fno);
        if (r != FR_OK) return r;
        const char *name = s_fno.altname[0] ? s_fno.altname : s_fno.fname;
        size_t n = strlen(name);
        if (olen + 1 + n > SHELL_PATH_MAX) return FR_INVALID_NAME;
        out[olen++] = '/';
        memcpy(out + olen, name, n);
        olen += n;
        p = slash ? slash + 1 : path + end;
    }
    out[olen] = '\0';
    return FR_OK;
}

static bool stop(void *ctx) {
    return shell_interrupted(ctx);
}

typedef struct {
    shell_t          *sh;
    const uf2_info_t *info;
} progress_t;

/* Before each sector is written: count it on the line, which has no
 * name so it stays short of the width and CR can rewrite it, and keep
 * the keyboard polled; a stop is ignored. */
static void tick(void *ctx) {
    progress_t *p = ctx;
    shell_t *sh = p->sh;
    out_printf(&sh->con, "\rwriting %lu of %lu sectors", (unsigned long)p->info->written + 1,
               (unsigned long)p->info->to_write);
    if (sh->hooks.interrupted) sh->hooks.interrupted(sh->hooks.ctx);
}

static uint32_t since(shell_t *sh, uint64_t t0) {
    return (uint32_t)(sh->hooks.now_us(sh->hooks.ctx) - t0);
}

/* Check, flash if need be, and reboot into the program at `path`,
 * which exists. `ram_only` for the menu, which must never be flashed. */
static int run_path(shell_t *sh, const char *name, const char *path, bool ram_only) {
    const shell_loader_t *ld = sh->loader;
    if (!ld || ld->problem) {
        shell_error(sh, "%s: cannot run programs: %s\n", name,
                    ld ? ld->problem : "no UF2 Loader here");
        return 1;
    }
    FRESULT fr = f_open(&s_prog, path, FA_READ);
    if (fr != FR_OK) {
        shell_error(sh, "%s: %s: %s\n", name, path, files_why(fr));
        return 1;
    }
    uf2_info_t info;
    uint64_t t0 = sh->hooks.now_us(sh->hooks.ctx);
    uf2_result_t r = uf2_check(&s_prog, &ld->flash, &info, stop, sh);
    info.check_us = since(sh, t0);
    if (r == UF2_FLASH && ram_only) {
        f_close(&s_prog);
        shell_error(sh, "%s: %s: not a RAM program\n", name, path);
        return 1;
    }
    if (r == UF2_FLASH && info.differ) {
        progress_t p = { sh, &info };
        t0 = sh->hooks.now_us(sh->hooks.ctx);
        r = uf2_write(&s_prog, &ld->flash, &info, tick, &p);
        info.write_us = since(sh, t0);
        out_puts(&sh->con, "\n");
    }
    f_close(&s_prog);

    switch (r) {
    case UF2_FLASH:
        shell_leaving(sh);
        ld->boot(ld->ctx, NULL, &info);
        return 0;
    case UF2_RAM:
        fr = short_path(path, s_sfn);
        if (fr != FR_OK) {
            shell_error(sh, "%s: %s\n", name, files_why(fr));
            return 1;
        }
        shell_leaving(sh);
        ld->boot(ld->ctx, s_sfn, &info);
        return 0;
    case UF2_NOT_UF2:
        shell_error(sh, "%s: not a UF2 program\n", name);
        return 126;
    case UF2_NOT_HERE:
        shell_error(sh, "%s: not for this device\n", name);
        return 126;
    case UF2_BAD:
        shell_error(sh, "%s: damaged UF2: blocks missing or out of order\n", name);
        return 126;
    case UF2_CARD:
        shell_error(sh, "%s: %s\n", name, files_why(info.fr));
        return 1;
    case UF2_STOPPED:
        return 130;
    case UF2_WRITE_FAILED:
        break;
    }
    shell_error(sh, "%s: writing flash failed (%s); flash may not hold a whole program\n", name,
                info.fr != FR_OK ? files_why(info.fr) : "flash error");
    return 1;
}

/* `name.uf2` in the directory `dir`, as typed in $PATH: into s_path if
 * it is there. */
static bool in_dir(shell_t *sh, const char *dir, const char *file) {
    static char abs[SHELL_PATH_MAX + 1];
    return flavour_resolve(abs, sh->cwd, dir) && path_join(s_path, abs, file) &&
           f_stat(s_path, &s_fno) == FR_OK && !(s_fno.fattrib & AM_DIR);
}

/* `name` as a program: a path if it has a '/' (in DOS, a '\' or a
 * drive), otherwise name.uf2 in the first directory of $PATH that has
 * it, the directories separated by ':' (DOS: ';', after the current
 * directory, as DOS looked). False, with the error printed, if none. */
static bool find(shell_t *sh, const char *name) {
    static char file[SHELL_PATH_MAX + 1], dir[SHELL_PATH_MAX + 1];
    size_t n = strlen(name);
    FRESULT r;
    if (flavour_is_path(name)) {
        if (!flavour_resolve(s_path, sh->cwd, name)) {
            shell_error(sh, "%s: name too long\n", name);
            return false;
        }
        r = f_stat(s_path, &s_fno);
        if (r == FR_OK && !(s_fno.fattrib & AM_DIR)) return true;
        if (r == FR_OK) shell_error(sh, "%s: is a directory\n", name);
        else shell_error(sh, "%s: %s\n", name, files_why(r));
        return false;
    }
    if (n + sizeof SHELL_PROGRAM_EXT <= sizeof file) {
        memcpy(file, name, n);
        memcpy(file + n, SHELL_PROGRAM_EXT, sizeof SHELL_PROGRAM_EXT);
        if (SHELL_DOS && in_dir(sh, ".", file)) return true;
        /* An empty entry is the current directory, as in sh. */
        for (const char *p = vars_get("PATH", 4); p;) {
            const char *colon = strchr(p, FLAVOUR_PATH_SEP);
            size_t len = colon ? (size_t)(colon - p) : strlen(p);
            if (len < sizeof dir) {
                memcpy(dir, p, len);
                dir[len] = '\0';
                if (len == 0) strcpy(dir, ".");
                if (in_dir(sh, dir, file)) return true;
            }
            p = colon ? colon + 1 : NULL;
        }
    }
    /* A name not found is "not found", whatever stopped the search, as
     * in sh. */
    flavour_not_found(sh, name);
    return false;
}

int run_program(shell_t *sh, int argc, char **argv, bool redirected) {
    if (!find(sh, argv[0])) return 127;
    /* Arguments are deferred (design.md §3.5); none is ignored. */
    if (argc > 1 || redirected) {
        shell_error(sh, "%s: a program takes no arguments, redirection or pipe yet\n", argv[0]);
        return 2;
    }
    return run_path(sh, argv[0], s_path, false);
}

int run_ram(shell_t *sh, const char *name, const char *path) {
    snprintf(s_path, sizeof s_path, "%s", path);
    return run_path(sh, name, s_path, true);
}

int run_ram_program(shell_t *sh, const char *name) {
    if (!find(sh, name)) return 127;
    return run_path(sh, name, s_path, true);
}

int cmd_menu(shell_t *sh, int argc, char **argv) {
    /* A redirection is refused by run_cmd, before it is opened. */
    if (argc > 1) {
        shell_error(sh, "usage: menu, with no operand or redirection\n");
        return 1;
    }
    (void)argv;
    return run_ram(sh, "menu", SHELL_MENU_UF2);
}
