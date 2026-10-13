/* input.c — what a command reads (input.h). */

#include "input.h"

#include <string.h>

#include "pipe.h"

FRESULT input_read(const input_t *in, void *buf, UINT n, UINT *got) {
    if (in->file) return f_read(in->file, buf, n, got);
    if (in->pipe) return pipe_read(in->pipe, buf, n, got);
    *got = 0;
    return FR_OK;
}

void reader_init(reader_t *rd, const input_t *in) {
    rd->in = *in;
    rd->r = FR_OK;
    rd->pos = rd->len = 0;
}

long reader_line(reader_t *rd, char *line, size_t size, bool *whole) {
    size_t n = 0;
    bool any = false;
    *whole = true;
    for (;;) {
        if (rd->pos == rd->len) {
            if (rd->r != FR_OK) return -1;
            rd->r = input_read(&rd->in, rd->buf, sizeof rd->buf, &rd->len);
            rd->pos = 0;
            if (rd->r != FR_OK) return -1;
            if (rd->len == 0) {
                if (!any) return -1;
                break;   /* a last line with no LF */
            }
        }
        char c = rd->buf[rd->pos];
        any = true;
        if (c == '\n') {
            rd->pos++;
            break;
        }
        if (n + 1 == size) {
            *whole = false;
            break;
        }
        line[n++] = c;
        rd->pos++;
    }
    if (*whole && n > 0 && line[n - 1] == '\r') n--;
    line[n] = '\0';
    return (long)n;
}
