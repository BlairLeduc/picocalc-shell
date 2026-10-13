/* out.c — the shell's output, LF to CR LF (out.h). */

#include "out.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

void out_write(const out_t *o, const char *s, size_t n) {
    char chunk[SHELL_OUT_CHUNK];
    size_t len = 0;
    for (size_t i = 0; i < n; i++) {
        if (len + 2u > sizeof chunk) {
            o->write(o->ctx, chunk, len);
            len = 0;
        }
        if (s[i] == '\n') chunk[len++] = '\r';
        chunk[len++] = s[i];
    }
    if (len) o->write(o->ctx, chunk, len);
}

void out_puts(const out_t *o, const char *s) {
    out_write(o, s, strlen(s));
}

void out_printf(const out_t *o, const char *fmt, ...) {
    char buf[SHELL_PRINTF_MAX];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;   /* truncated, never overrun */
    out_write(o, buf, (size_t)n);
}
