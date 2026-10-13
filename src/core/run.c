/* run.c — programs and `menu` (run.h). */

#include "run.h"

#include <string.h>

#include "files.h"
#include "path.h"

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

/* Keep the keyboard polled while flash is written; a stop is ignored. */
static void tick(void *ctx) {
    shell_t *sh = ctx;
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
        out_printf(&sh->con, "%s: writing %lu of %lu sectors\n", name,
                   (unsigned long)info.to_write, (unsigned long)info.sectors);
        t0 = sh->hooks.now_us(sh->hooks.ctx);
        r = uf2_write(&s_prog, &ld->flash, &info, tick, sh);
        info.write_us = since(sh, t0);
    }
    f_close(&s_prog);

    switch (r) {
    case UF2_FLASH:
        ld->boot(ld->ctx, NULL, &info);
        return 0;
    case UF2_RAM:
        fr = short_path(path, s_sfn);
        if (fr != FR_OK) {
            shell_error(sh, "%s: %s\n", name, files_why(fr));
            return 1;
        }
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

/* `name` as a program: a path if it has a '/', otherwise name.uf2 in
 * the program directory. False, with the error printed, if none. */
static bool find(shell_t *sh, const char *name) {
    char file[SHELL_PATH_MAX + 1];
    bool is_path = strchr(name, '/') != NULL;
    size_t n = strlen(name);
    bool ok;
    if (is_path) {
        ok = path_resolve(s_path, sh->cwd, name);
    } else {
        ok = n + sizeof SHELL_PROGRAM_EXT <= sizeof file;
        if (ok) {
            memcpy(file, name, n);
            memcpy(file + n, SHELL_PROGRAM_EXT, sizeof SHELL_PROGRAM_EXT);
            ok = path_join(s_path, SHELL_PROGRAM_DIR, file);
        }
    }
    if (!ok) {
        shell_error(sh, "%s: name too long\n", name);
        return false;
    }
    FRESULT r = f_stat(s_path, &s_fno);
    if (r == FR_OK && !(s_fno.fattrib & AM_DIR)) return true;
    /* A name not found is "not found", whatever stopped the search, as
     * in sh; a path says why. */
    if (!is_path) shell_error(sh, "%s: not found\n", name);
    else if (r == FR_OK) shell_error(sh, "%s: is a directory\n", name);
    else shell_error(sh, "%s: %s\n", name, files_why(r));
    return false;
}

int run_program(shell_t *sh, int argc, char **argv, bool redirected) {
    if (!find(sh, argv[0])) return 127;
    /* Arguments are deferred (design.md §3.5); none is ignored. */
    if (argc > 1 || redirected) {
        shell_error(sh, "%s: a program takes no arguments or redirection yet\n", argv[0]);
        return 2;
    }
    return run_path(sh, argv[0], s_path, false);
}

int cmd_menu(shell_t *sh, int argc, char **argv) {
    /* A redirection would be left as name.new by the reboot. */
    if (argc > 1 || !sh->out_is_con || sh->in) {
        shell_error(sh, "usage: menu, with no operand or redirection\n");
        return 1;
    }
    (void)argv;
    strcpy(s_path, SHELL_MENU_UF2);
    return run_path(sh, "menu", s_path, true);
}
