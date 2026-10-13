/* out.h — where the shell's output goes (design.md §4.1, §5).
 *
 * The port gives a sink: on the board, the terminal's grid, the panel and
 * the UART mirror; on the host, a test's grid or the macOS terminal. The
 * shell writes text with '\n' line ends and the sink receives "\r\n", as
 * the starter's stdio did, since the terminal's LF only moves down. A
 * raw sink, such as a file a command's output is redirected to, gets
 * the bytes as they are. */
#ifndef SHELL_OUT_H
#define SHELL_OUT_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    void (*write)(void *ctx, const char *buf, size_t n);
    void *ctx;
    bool raw;    /* no LF to CR LF */
} out_t;

/* Each call reaches the sink as whole chunks of SHELL_OUT_CHUNK bytes or
 * fewer, in order; a short write is one call. A raw sink gets each call
 * as one write. */
void out_write(const out_t *o, const char *s, size_t n);
void out_puts(const out_t *o, const char *s);
void out_printf(const out_t *o, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#endif /* SHELL_OUT_H */
