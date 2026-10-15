/* dos_text.c — TYPE, MORE and FIND for the DOS flavour (dos.h; design.md
 * §4.7). MORE is the UNIX flavour's `more` (text.h); TYPE and FIND read
 * as `cat` and `grep` do (input.h). */

#include <string.h>

#include "dos.h"
#include "files.h"
#include "text.h"

static FIL      s_file;
static reader_t s_rd;
static char     s_line[SHELL_TEXT_LINE];

/* TYPE file: the file as it is. */
int dos_type(shell_t *sh, int argc, char **argv) {
    if (argc < 2) {
        dos_error(sh, "Required parameter missing", NULL);
        return 1;
    }
    if (dos_too_many(sh, argc, argv, 2)) return 1;
    if (dos_wild(argv[1])) {
        dos_error(sh, "Invalid filename or file not found", NULL);
        return 1;
    }
    if (files_open_read(sh, "TYPE", argv[1], &s_file) != FR_OK) return 1;
    int status = 0;
    for (;;) {
        UINT n;
        FRESULT r = f_read(&s_file, s_line, sizeof s_line, &n);
        if (r != FR_OK) {
            dos_error(sh, dos_why(r), argv[1]);
            status = 1;
            break;
        }
        if (n == 0) break;
        out_write(&sh->out, s_line, n);
        if (shell_interrupted(sh)) {
            status = 1;
            break;
        }
    }
    f_close(&s_file);
    return status;
}

/* MORE < file, command | MORE, or MORE file. */
int dos_more(shell_t *sh, int argc, char **argv) {
    if (dos_too_many(sh, argc, argv, 2)) return 1;
    if (argc == 1 && !input_any(&sh->in)) {
        dos_error(sh, "Required parameter missing", NULL);
        return 1;
    }
    return cmd_more(sh, argc, argv);
}

/* ---- FIND ------------------------------------------------------------- */

enum { FIND_V = 1u << 0, FIND_C = 1u << 1, FIND_N = 1u << 2, FIND_I = 1u << 3 };

static char fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

/* Whether `text` holds `what`, letters folded with /I. */
static bool contains(const char *text, const char *what, bool nocase) {
    size_t n = strlen(what);
    if (n == 0) return true;
    for (; *text; text++) {
        size_t i = 0;
        while (i < n && text[i] &&
               (nocase ? fold(text[i]) == fold(what[i]) : text[i] == what[i]))
            i++;
        if (i == n) return true;
    }
    return false;
}

/* One input's lines that hold (or with /V, do not hold) `what`: how
 * many, or -1 if it could not be read. */
static long find_in(shell_t *sh, const input_t *in, const char *what, unsigned flags) {
    reader_init(&s_rd, in);
    long count = 0;
    unsigned long lineno = 0;
    bool whole = true, start = true;
    while (reader_line(&s_rd, s_line, sizeof s_line, &whole) >= 0) {
        if (start) lineno++;
        bool hit = contains(s_line, what, flags & FIND_I) != ((flags & FIND_V) != 0);
        if (hit) {
            if (start) count++;
            if (!(flags & FIND_C)) {
                if ((flags & FIND_N) && start) out_printf(&sh->out, "[%lu]", lineno);
                out_puts(&sh->out, s_line);
                if (whole) out_puts(&sh->out, "\n");
            }
        }
        start = whole;
        if (shell_interrupted(sh)) break;
    }
    return s_rd.r == FR_OK ? count : -1;
}

/* The name as DOS showed it in FIND's rule: in capitals. */
static void rule(shell_t *sh, const char *name, const char *tail) {
    out_puts(&sh->out, "---------- ");
    for (const char *s = name; *s; s++) {
        char c = fold(*s);
        out_write(&sh->out, &c, 1);
    }
    out_puts(&sh->out, tail);
}

int dos_find(shell_t *sh, int argc, char **argv) {
    unsigned flags;
    if (!dos_switches(sh, &argc, argv, "VCNI", &flags)) return 2;
    if (argc < 2 || (argc == 2 && !input_any(&sh->in))) {
        shell_error(sh, "FIND: Required parameter missing\n");
        return 2;
    }
    const char *what = argv[1];
    bool found = false;
    if (argc == 2) {
        long n = find_in(sh, &sh->in, what, flags);
        if (n < 0) {
            dos_error(sh, dos_why(s_rd.r), NULL);
            return 2;
        }
        if (flags & FIND_C) out_printf(&sh->out, "%ld\n", n);
        return n > 0 ? 0 : 1;
    }
    int status = 0;
    for (int i = 2; i < argc && !sh->stop; i++) {
        if (files_open_read(sh, "FIND", argv[i], &s_file) != FR_OK) {
            status = 2;
            continue;
        }
        if (!(flags & FIND_C)) {
            out_puts(&sh->out, "\n");
            rule(sh, argv[i], "\n");
        }
        long n = find_in(sh, &(input_t){ &s_file, NULL }, what, flags);
        f_close(&s_file);
        if (n < 0) {
            dos_error(sh, dos_why(s_rd.r), argv[i]);
            status = 2;
            continue;
        }
        if (flags & FIND_C) {
            rule(sh, argv[i], "");
            out_printf(&sh->out, ": %ld\n", n);
        }
        found |= n > 0;
    }
    return status ? status : found ? 0 : 1;
}
