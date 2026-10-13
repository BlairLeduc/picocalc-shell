/* path.h — UNIX paths on the card (design.md §4.2).
 *
 * The shell keeps the current directory itself and hands FatFs absolute
 * paths only (FF_FS_RPATH 0), so a card changed under the shell cannot
 * leave FatFs's idea of it and the prompt's disagreeing. Pure string
 * work, no FatFs. */
#ifndef SHELL_PATH_H
#define SHELL_PATH_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

/* `arg` made absolute against `cwd` (itself absolute and normal), with
 * `.`, `..` and repeated or trailing '/' removed: "/", "/home",
 * "/oric/keymaps". `..` at the root stays there. False if the result,
 * or a step on the way, does not fit SHELL_PATH_MAX. */
bool path_resolve(char out[SHELL_PATH_MAX + 1], const char *cwd, const char *arg);

/* The last component of a normal path; "" for "/". */
const char *path_base(const char *path);

/* dir + '/' + name into out, a normal path when both are. False if it
 * does not fit. */
bool path_join(char out[SHELL_PATH_MAX + 1], const char *dir, const char *name);

/* Whether a and b name the same path, as FAT compares names: ASCII
 * letters without regard to case. */
bool path_same(const char *a, const char *b);

/* Whether `inner` is `outer` or below it, comparing as path_same. */
bool path_within(const char *inner, const char *outer);

/* Whether `name` matches the glob `pat` (design.md §4.4): `*`, `?`, and
 * `[…]` with ranges and `!` or `^` to negate; `\` makes the next
 * character literal. Letters match without regard to case, as FAT names
 * do. A '[' never closed is an ordinary character. */
bool path_match(const char *pat, const char *name);

#endif /* SHELL_PATH_H */
