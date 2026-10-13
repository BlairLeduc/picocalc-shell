/* vars.c — the shell's variables (vars.h). */

#include "vars.h"

#include <string.h>

#include "config.h"

/* NAME=value\0 ... with an empty string after the last. */
static char   s_vars[SHELL_VARS_BYTES];
static size_t s_used;   /* bytes in use, not counting the final NUL */

void vars_init(void) {
    s_used = 0;
    s_vars[0] = '\0';
}

bool vars_is_name(const char *s, size_t len) {
    if (len == 0) return false;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
        if (!letter && !(i > 0 && c >= '0' && c <= '9')) return false;
    }
    return true;
}

/* The entry for NAME, or NULL. */
static char *find(const char *name, size_t len) {
    for (char *e = s_vars; *e; e += strlen(e) + 1)
        if (strncmp(e, name, len) == 0 && e[len] == '=') return e;
    return NULL;
}

const char *vars_get(const char *name, size_t len) {
    const char *e = find(name, len);
    return e ? e + len + 1 : NULL;
}

void vars_unset(const char *name, size_t len) {
    char *e = find(name, len);
    if (!e) return;
    size_t n = strlen(e) + 1;
    memmove(e, e + n, (size_t)(s_vars + s_used + 1 - (e + n)));
    s_used -= n;
}

bool vars_set(const char *name, size_t len, const char *value) {
    size_t vlen = strlen(value);
    const char *old = find(name, len);
    size_t freed = old ? strlen(old) + 1 : 0;
    /* The new entry, its NUL, and the final NUL. */
    if (s_used - freed + len + 1 + vlen + 1 + 1 > sizeof s_vars) return false;
    /* value may point into the old entry, which unset moves; static, as
     * the stack is small. */
    static char copy[SHELL_VARS_BYTES];
    memcpy(copy, value, vlen + 1);
    vars_unset(name, len);
    char *e = s_vars + s_used;
    memcpy(e, name, len);
    e[len] = '=';
    memcpy(e + len + 1, copy, vlen + 1);
    s_used += len + 1 + vlen + 1;
    s_vars[s_used] = '\0';
    return true;
}

const char *vars_at(unsigned i) {
    for (const char *e = s_vars; *e; e += strlen(e) + 1)
        if (i-- == 0) return e;
    return NULL;
}
