/* editor_file.c — a text file into an editor's buffer and back
 * (editor_file.h). Moved out of vi.c for EDIT. */

#include "editor_file.h"

#include <stdio.h>

#include "files.h"

/* Static, as in files.c: the shell's stack is small (design.md §7). */
static char    s_text[512];   /* a file's bytes on their way */
static FIL     s_fil;
static FILINFO s_fno;
static files_safe_t s_save;

const char *editor_file_load(const char *path, char *buf, size_t cap, size_t *len,
                             editor_file_t *f, unsigned long *bytes, char *why, size_t why_cap) {
    *bytes = 0;
    FRESULT r = files_stat(path, &s_fno);
    if (r == FR_NO_FILE) {
        *len = 0;
        buf[0] = '\0';
        f->crlf = false;
        f->eol = f->was_empty = f->is_new = true;
        return NULL;
    }
    if (r != FR_OK) return files_why(r);
    if (s_fno.fattrib & AM_DIR) return "is a directory";
    if (s_fno.fsize > cap - 1) {
        snprintf(why, why_cap, "too large: %lu bytes, %lu at most", (unsigned long)s_fno.fsize,
                 (unsigned long)(cap - 1));
        return why;
    }
    r = f_open(&s_fil, path, FA_READ);
    if (r != FR_OK) return files_why(r);
    /* From here the buffer is the file's, whole or empty. */
    UINT got = 0;
    r = f_read(&s_fil, buf, (UINT)s_fno.fsize, &got);
    f_close(&s_fil);
    *len = 0;
    if (r != FR_OK || got != s_fno.fsize) {
        buf[0] = '\0';
        return r != FR_OK ? files_why(r) : "card I/O error";
    }
    size_t n = got;
    if (s_fno.fattrib & AM_RDO) f->readonly = true;
    f->is_new = false;

    /* CR LF if every line break is one; then the CRs come off. */
    size_t lfs = 0, crlfs = 0;
    for (size_t i = 0; i < n; i++) {
        if (buf[i] != '\n') continue;
        lfs++;
        crlfs += i > 0 && buf[i - 1] == '\r';
    }
    f->crlf = lfs > 0 && crlfs == lfs;
    if (f->crlf) {
        size_t o = 0;
        for (size_t i = 0; i < n; i++)
            if (!(buf[i] == '\r' && i + 1 < n && buf[i + 1] == '\n')) buf[o++] = buf[i];
        n = o;
    }
    f->was_empty = n == 0;
    f->eol = n == 0 || buf[n - 1] == '\n';
    if (n > 0 && buf[n - 1] == '\n') n--;
    *len = n;
    buf[n] = '\0';
    *bytes = (unsigned long)s_fno.fsize;
    return NULL;
}

const char *editor_file_save(const char *path, const char *buf, size_t len, editor_file_t *f,
                             unsigned long *bytes) {
    *bytes = 0;
    const char *why = files_safe_open(&s_save, path);
    if (why) return why;
    size_t n = 0;
    bool last = f->eol && !(len == 0 && f->was_empty);
    for (size_t i = 0; i <= len; i++) {
        if (n + 2 > sizeof s_text || (i == len && n)) {
            files_safe_write(&s_save, s_text, n);
            *bytes += n;
            n = 0;
        }
        if (i == len) break;
        if (buf[i] == '\n' && f->crlf) s_text[n++] = '\r';
        s_text[n++] = buf[i];
    }
    if (last) {
        files_safe_write(&s_save, f->crlf ? "\r\n" : "\n", f->crlf ? 2 : 1);
        *bytes += f->crlf ? 2 : 1;
    }
    why = files_safe_commit(&s_save);
    if (why) return why;
    f->is_new = false;
    f->was_empty = len == 0 && f->was_empty;
    return NULL;
}
