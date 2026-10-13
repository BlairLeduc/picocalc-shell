/* vars.h — the shell's variables (design.md §4.4).
 *
 * For the session only: they start as the defaults below each time the
 * shell starts, and /etc/profile can change them. There is no
 * environment to pass on, since programs take no arguments (§3.5), so
 * `export` is the same as setting one. Packed as NAME=value strings in
 * one buffer of SHELL_VARS_BYTES, in the order they were set. */
#ifndef SHELL_VARS_H
#define SHELL_VARS_H

#include <stdbool.h>
#include <stddef.h>

/* None set. */
void vars_init(void);

/* The value of NAME, or NULL if it is not set. `len` is the name's
 * length, so a name inside a longer string can be looked up. */
const char *vars_get(const char *name, size_t len);

/* Set NAME to value: false if there is no room, and NAME keeps the
 * value it had. */
bool vars_set(const char *name, size_t len, const char *value);

void vars_unset(const char *name, size_t len);

/* Whether s[0..len) is a name: a letter or '_', then letters, digits
 * and '_'. */
bool vars_is_name(const char *s, size_t len);

/* Each variable as "NAME=value", in the order set: the i-th, or NULL
 * past the last. */
const char *vars_at(unsigned i);

#endif /* SHELL_VARS_H */
