/* path.c — UNIX paths on the card (path.h). */

#include "path.h"

#include <string.h>

bool path_resolve(char out[SHELL_PATH_MAX + 1], const char *cwd, const char *arg) {
    size_t len = 0;
    out[0] = '\0';
    /* Two passes over one loop: the directory unless arg is absolute,
     * then arg. Each component is appended, or for "..", removed. */
    const char *parts[2] = { arg[0] == '/' ? "" : cwd, arg };
    for (int i = 0; i < 2; i++) {
        const char *s = parts[i];
        while (*s) {
            while (*s == '/') s++;
            const char *e = s;
            while (*e && *e != '/') e++;
            size_t n = (size_t)(e - s);
            if (n == 0 || (n == 1 && s[0] == '.')) {
                /* nothing */
            } else if (n == 2 && s[0] == '.' && s[1] == '.') {
                while (len > 0 && out[len - 1] != '/') len--;
                if (len > 0) len--;   /* the '/' before it */
            } else {
                if (len + 1u + n > SHELL_PATH_MAX) return false;
                out[len++] = '/';
                memcpy(out + len, s, n);
                len += n;
            }
            s = e;
        }
    }
    if (len == 0) out[len++] = '/';
    out[len] = '\0';
    return true;
}

const char *path_base(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

bool path_join(char out[SHELL_PATH_MAX + 1], const char *dir, const char *name) {
    size_t d = strlen(dir), n = strlen(name);
    if (d == 1 && dir[0] == '/') d = 0;
    if (d + 1u + n > SHELL_PATH_MAX) return false;
    memmove(out, dir, d);
    out[d] = '/';
    memcpy(out + d + 1, name, n + 1);
    return true;
}

static char fold(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static size_t common(const char *a, const char *b) {
    size_t i = 0;
    while (a[i] && b[i] && fold(a[i]) == fold(b[i])) i++;
    return i;
}

bool path_same(const char *a, const char *b) {
    size_t i = common(a, b);
    return a[i] == '\0' && b[i] == '\0';
}

bool path_within(const char *inner, const char *outer) {
    if (strcmp(outer, "/") == 0) return true;
    size_t i = common(inner, outer);
    return outer[i] == '\0' && (inner[i] == '\0' || inner[i] == '/');
}

/* One bracket expression at *pp, just past its '['; c is the character
 * to test. *pp moves past the closing ']'. False with *pp unchanged if
 * the bracket is never closed, so '[' is then an ordinary character. */
static bool bracket(const char **pp, char c, bool *in) {
    const char *p = *pp;
    bool negate = *p == '!' || *p == '^';
    if (negate) p++;
    bool hit = false;
    /* A ']' first is a member, not the end. */
    for (bool first = true; *p && (first || *p != ']'); first = false) {
        char lo = *p == '\\' && p[1] ? *++p : *p;
        p++;
        char hi = lo;
        if (p[0] == '-' && p[1] && p[1] != ']') {
            p++;
            hi = *p == '\\' && p[1] ? *++p : *p;
            p++;
        }
        if (fold(c) >= fold(lo) && fold(c) <= fold(hi)) hit = true;
    }
    if (*p != ']') return false;
    *pp = p + 1;
    *in = hit != negate;
    return true;
}

bool path_match(const char *pat, const char *name) {
    /* The last '*' and where its run began: on a mismatch, it takes one
     * more character and matching resumes after it. */
    const char *star = NULL, *resume = NULL;
    for (;;) {
        if (*pat == '*') {
            while (*pat == '*') pat++;
            star = pat;
            resume = name;
            continue;
        }
        if (!*name) return !*pat;
        const char *p = pat;
        bool ok;
        if (*p == '?') {
            ok = true;
            p++;
        } else if (*p == '[') {
            const char *q = p + 1;
            if (bracket(&q, *name, &ok)) {
                p = q;
            } else {
                ok = *name == '[';
                p++;
            }
        } else {
            if (*p == '\\' && p[1]) p++;
            ok = *p && fold(*p) == fold(*name);
            p++;
        }
        if (ok) {
            pat = p;
            name++;
        } else if (star) {
            pat = star;
            name = ++resume;
        } else {
            return false;
        }
    }
}
