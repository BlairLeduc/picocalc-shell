/* pipe.h — `a | b` (design.md §4.4).
 *
 * Every builtin runs in the one process, so `a` runs to completion into
 * the pipe and `b` then reads it. A pipe takes one of SHELL_PIPE_RAM
 * buffers of SHELL_PIPE_BUF bytes if one is free; once its output is
 * larger, or if none is free, it goes through a file in /.shell, which
 * is removed when the pipe is closed. Such a file is the shell's own,
 * not a user's, so it is written in place, not through name.new. */
#ifndef SHELL_PIPE_H
#define SHELL_PIPE_H

#include <stdbool.h>

#include "ff.h"
#include "out.h"

typedef struct pipe pipe_t;

/* An empty pipe to write; NULL, with why set, if none is free. */
pipe_t *pipe_open(const char **why);

/* A raw sink that writes to it. */
out_t pipe_out(pipe_t *p);

/* Done writing; the next read starts at the beginning. NULL, or why
 * some of the output was lost (the reader gets what was kept). */
const char *pipe_written(pipe_t *p);

/* Read up to n bytes, *got of them; 0 at the end. */
FRESULT pipe_read(pipe_t *p, void *buf, UINT n, UINT *got);

/* Free it, and remove its file if it had one. */
void pipe_close(pipe_t *p);

#endif /* SHELL_PIPE_H */
