/* pipe.c — `a | b` (pipe.h). */

#include "pipe.h"

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "files.h"

#define PIPE_DIR "/.shell"

struct pipe {
    bool        used;
    bool        spilled;        /* the data is in the file, not in ram */
    char       *ram;            /* a buffer of SHELL_PIPE_BUF, or NULL */
    size_t      len, pos;       /* in ram */
    const char *why;            /* the first failure while writing */
    FIL         fil;
    char        path[sizeof PIPE_DIR "/pipe00"];
};

static pipe_t s_pipes[SHELL_PIPES];
static char   s_ram[SHELL_PIPE_RAM][SHELL_PIPE_BUF];
static bool   s_ram_used[SHELL_PIPE_RAM];

pipe_t *pipe_open(const char **why) {
    for (unsigned i = 0; i < SHELL_PIPES; i++) {
        pipe_t *p = &s_pipes[i];
        if (p->used) continue;
        memset(p, 0, sizeof *p);
        p->used = true;
        snprintf(p->path, sizeof p->path, PIPE_DIR "/pipe%02u", i);
        for (unsigned b = 0; b < SHELL_PIPE_RAM; b++) {
            if (!s_ram_used[b]) {
                s_ram_used[b] = true;
                p->ram = s_ram[b];
                break;
            }
        }
        return p;
    }
    *why = "too many pipes";
    return NULL;
}

/* Move what is in ram to the file, and write to the file from now on. */
static void spill(pipe_t *p) {
    FRESULT r = f_mkdir(PIPE_DIR);
    if (r == FR_OK || r == FR_EXIST) r = f_open(&p->fil, p->path, FA_CREATE_ALWAYS | FA_WRITE | FA_READ);
    if (r != FR_OK) {
        p->why = files_why(r);
        return;
    }
    p->spilled = true;
    if (p->len) {
        UINT done;
        r = f_write(&p->fil, p->ram, (UINT)p->len, &done);
        if (r != FR_OK) p->why = files_why(r);
        else if (done < p->len) p->why = "no space left on the card";
    }
}

static void pipe_write(void *ctx, const char *buf, size_t n) {
    pipe_t *p = ctx;
    if (p->why) return;
    if (!p->spilled && p->ram && p->len + n <= SHELL_PIPE_BUF) {
        memcpy(p->ram + p->len, buf, n);
        p->len += n;
        return;
    }
    if (!p->spilled) spill(p);
    if (p->why) return;
    UINT done;
    FRESULT r = f_write(&p->fil, buf, (UINT)n, &done);
    if (r != FR_OK) p->why = files_why(r);
    else if (done < n) p->why = "no space left on the card";
}

out_t pipe_out(pipe_t *p) {
    return (out_t){ pipe_write, p, true };
}

const char *pipe_written(pipe_t *p) {
    p->pos = 0;
    if (p->spilled) {
        FRESULT r = f_lseek(&p->fil, 0);
        if (r != FR_OK && !p->why) p->why = files_why(r);
    }
    return p->why;
}

FRESULT pipe_read(pipe_t *p, void *buf, UINT n, UINT *got) {
    if (p->spilled) return f_read(&p->fil, buf, n, got);
    size_t left = p->len - p->pos;
    if (n > left) n = (UINT)left;
    memcpy(buf, p->ram + p->pos, n);
    p->pos += n;
    *got = n;
    return FR_OK;
}

void pipe_close(pipe_t *p) {
    if (p->spilled) {
        f_close(&p->fil);
        f_unlink(p->path);
    }
    for (unsigned b = 0; b < SHELL_PIPE_RAM; b++)
        if (p->ram == s_ram[b]) s_ram_used[b] = false;
    p->used = false;
}
