/* editor_file.h — a text file into an editor's buffer and back
 * (design.md §4.6, §4.7): vi's rules, shared with EDIT.
 *
 *   - The whole file is read into one buffer; a larger file is refused,
 *     never truncated.
 *   - If every line break is CR LF, the CRs come off on reading and go
 *     back on writing; otherwise a CR is a character like any other.
 *     Whether the file ends in a line break is kept too.
 *   - A file FAT marks read-only opens read-only.
 *   - Saving goes through name.new and a rename (files.h). */
#ifndef SHELL_EDITOR_FILE_H
#define SHELL_EDITOR_FILE_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool readonly;   /* FAT marks it so */
    bool crlf;       /* every line break in the file was CR LF */
    bool eol;        /* a line break follows the last line */
    bool was_empty;  /* the file was empty or new */
    bool is_new;     /* there was no such file */
} editor_file_t;

/* Read `path` into buf, which holds `cap` bytes with the NUL; *len is the
 * text's length and *bytes the file's. A missing file is a new, empty
 * one. NULL, or why it cannot be edited (the words may be in `why`, of
 * `why_cap` bytes); the buffer and *f are left as they were unless the
 * file was opened, and then emptied if it could not be read. */
const char *editor_file_load(const char *path, char *buf, size_t cap, size_t *len,
                             editor_file_t *f, unsigned long *bytes, char *why, size_t why_cap);

/* Write len bytes of buf to `path` as editor_file_load read it, through
 * name.new. NULL, with *bytes written, or why it was not written. */
const char *editor_file_save(const char *path, const char *buf, size_t len, editor_file_t *f,
                             unsigned long *bytes);

#endif /* SHELL_EDITOR_FILE_H */
