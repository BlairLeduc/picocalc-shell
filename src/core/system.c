/* system.c — the PicoCalc's own commands (system.h). */

#include "system.h"

#include <string.h>

#include "files.h"
#include "term.h"

static int usage(shell_t *sh, const char *text) {
    shell_error(sh, "usage: %s\n", text);
    return 1;
}

static int not_here(shell_t *sh, const char *cmd) {
    shell_error(sh, "%s: not on this machine\n", cmd);
    return 1;
}

/* ---- help ------------------------------------------------------------- */

int cmd_help(shell_t *sh, int argc, char **argv) {
    if (argc > 2) return usage(sh, "help [command]");
    const char *name;
    if (argc == 2) {
        for (unsigned i = 0; (name = shell_builtin_at(i)) != NULL; i++) {
            if (strcmp(name, argv[1]) == 0) {
                out_printf(&sh->out, "usage: %s\n%s\n", shell_builtin_usage(i),
                           shell_builtin_about(i));
                return 0;
            }
        }
        shell_error(sh, "help: %s: not a builtin; a program, if one has that name\n", argv[1]);
        return 1;
    }
    /* The names in columns as wide as the longest, to the screen's width. */
    size_t width = 0;
    for (unsigned i = 0; (name = shell_builtin_at(i)) != NULL; i++)
        if (strlen(name) > width) width = strlen(name);
    width += 2;
    unsigned per_row = (unsigned)(shell_cols(sh) / width);
    if (per_row == 0) per_row = 1;
    unsigned col = 0;
    for (unsigned i = 0; (name = shell_builtin_at(i)) != NULL; i++) {
        bool last = ++col == per_row || shell_builtin_at(i + 1) == NULL;
        if (last) {
            out_printf(&sh->out, "%s\n", name);
            col = 0;
        } else {
            out_printf(&sh->out, "%-*s", (int)width, name);
        }
    }
    out_puts(&sh->out, "\n`help name` for one. Any other name runs\n"
                       "a program from $PATH, as `name.uf2`.\n");
    return 0;
}

/* ---- the screen ------------------------------------------------------- */

int cmd_clear(shell_t *sh, int argc, char **argv) {
    (void)argv;
    if (argc != 1) return usage(sh, "clear");
    out_puts(&sh->out, "\033[H\033[2J");
    return 0;
}

static const struct {
    const char *name;
    uint16_t    rgb;
} k_phosphors[] = {
    { "default", TERM_FOREGROUND },
    { "white",   TERM_WHITE_PHOSPHOR },
    { "green",   TERM_GREEN_PHOSPHOR },
    { "amber",   TERM_AMBER_PHOSPHOR },
};

#define NPHOSPHORS (sizeof k_phosphors / sizeof k_phosphors[0])

int cmd_phosphor(shell_t *sh, int argc, char **argv) {
    static const char use[] = "phosphor [white|green|amber|default]";
    if (argc > 2) return usage(sh, use);
    if (argc == 1) {
        out_printf(&sh->out, "%s\n", k_phosphors[sh->phosphor].name);
        return 0;
    }
    for (unsigned i = 0; i < NPHOSPHORS; i++) {
        if (strcmp(argv[1], k_phosphors[i].name) != 0) continue;
        if (!sh->sys.phosphor) return not_here(sh, "phosphor");
        sh->sys.phosphor(sh->sys.ctx, k_phosphors[i].rgb);
        sh->phosphor = (uint8_t)i;
        /* The new ink from here on, not only after the next reset. */
        out_puts(&sh->con, "\033[39m");
        return 0;
    }
    shell_error(sh, "phosphor: %s: white, green, amber or default\n", argv[1]);
    return 1;
}

/* ---- the southbridge -------------------------------------------------- */

int cmd_battery(shell_t *sh, int argc, char **argv) {
    (void)argv;
    if (argc != 1) return usage(sh, "battery");
    if (!sh->sys.battery) return not_here(sh, "battery");
    unsigned pct;
    bool charging;
    const char *why = sh->sys.battery(sh->sys.ctx, &pct, &charging);
    if (why) {
        shell_error(sh, "battery: %s\n", why);
        return 1;
    }
    out_printf(&sh->out, "%u%%%s\n", pct, charging ? ", charging" : "");
    return 0;
}

static const char *const k_lights[] = { "lcd", "kbd" };

/* One backlight: set it unless `level` is negative, then show it. */
static int light(shell_t *sh, shell_light_t which, int level) {
    unsigned now;
    const char *why = sh->sys.backlight(sh->sys.ctx, which, level, &now);
    if (why) {
        shell_error(sh, "backlight: %s: %s\n", k_lights[which], why);
        return 1;
    }
    out_printf(&sh->out, "%s %u\n", k_lights[which], now);
    return 0;
}

int cmd_backlight(shell_t *sh, int argc, char **argv) {
    static const char use[] = "backlight [lcd|kbd] [0-255]";
    if (argc > 3) return usage(sh, use);
    int i = 1;
    int which = -1;
    for (unsigned k = 0; k < 2 && i < argc; k++) {
        if (strcmp(argv[i], k_lights[k]) == 0) {
            which = (int)k;
            i++;
            break;
        }
    }
    int level = -1;
    if (i < argc) {
        const char *p = argv[i++];
        level = 0;
        if (!*p) return usage(sh, use);
        for (; *p; p++) {
            if (*p < '0' || *p > '9' || (level = level * 10 + (*p - '0')) > 255) {
                shell_error(sh, "backlight: %s: a level is 0 to 255\n", argv[i - 1]);
                return 1;
            }
        }
        if (which < 0) which = SHELL_LIGHT_LCD;
    }
    if (i != argc) return usage(sh, use);
    if (!sh->sys.backlight) return not_here(sh, "backlight");
    if (which >= 0) return light(sh, (shell_light_t)which, level);
    return light(sh, SHELL_LIGHT_LCD, -1) | light(sh, SHELL_LIGHT_KBD, -1);
}

/* ---- info ------------------------------------------------------------- */

int cmd_info(shell_t *sh, int argc, char **argv) {
    (void)argv;
    if (argc != 1) return usage(sh, "info");
    const out_t *o = &sh->out;
    out_printf(o, "shell     %s\n", sh->version);
    if (sh->sys.info) sh->sys.info(sh->sys.ctx, o);
    char card[48];
    FRESULT r = files_card(card, sizeof card);
    out_printf(o, "card      %s\n", r == FR_OK ? card : files_why(r));
    if (sh->sys.battery) {
        unsigned pct;
        bool charging;
        const char *why = sh->sys.battery(sh->sys.ctx, &pct, &charging);
        if (why) out_printf(o, "battery   %s\n", why);
        else out_printf(o, "battery   %u%%%s\n", pct, charging ? ", charging" : "");
    }
    return 0;
}

/* ---- leaving ---------------------------------------------------------- */

/* Save the history, say what is happening, and go; back only if the
 * port could not. */
static int leave(shell_t *sh, int argc, const char *name, shell_power_t what, const char *msg) {
    if (argc != 1) return usage(sh, name);
    if (!sh->sys.power) return not_here(sh, name);
    shell_leaving(sh);
    out_puts(&sh->con, msg);
    const char *why = sh->sys.power(sh->sys.ctx, what);
    shell_error(sh, "%s: %s\n", name, why ? why : "it did not happen");
    return 1;
}

int cmd_poweroff(shell_t *sh, int argc, char **argv) {
    (void)argv;
    return leave(sh, argc, "poweroff", SHELL_POWER_OFF, "Switching off in a few seconds.\n");
}

int cmd_reboot(shell_t *sh, int argc, char **argv) {
    (void)argv;
    return leave(sh, argc, "reboot", SHELL_POWER_REBOOT, "Restarting the shell.\n");
}

int cmd_bootsel(shell_t *sh, int argc, char **argv) {
    (void)argv;
    return leave(sh, argc, "bootsel", SHELL_POWER_BOOTSEL,
                 "BOOTSEL: the RP2350 is now a USB drive.\n"
                 "Switch off and on to come back.\n");
}

bool system_leaves(shell_cmd_fn fn) {
    return fn == cmd_poweroff || fn == cmd_reboot || fn == cmd_bootsel;
}
