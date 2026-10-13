/* test_path.c — UNIX paths resolved by the shell (path.h, design.md §4.2). */

#include <string.h>

#include "path.h"
#include "test_util.h"

static char out[SHELL_PATH_MAX + 1];

static const char *res(const char *cwd, const char *arg) {
    return path_resolve(out, cwd, arg) ? out : "(too long)";
}

int main(void) {
    static const struct { const char *cwd, *arg, *want; } cases[] = {
        { "/",          "",             "/" },
        { "/",          ".",            "/" },
        { "/",          "..",           "/" },
        { "/",          "../../a",      "/a" },
        { "/",          "home",         "/home" },
        { "/home",      "notes.txt",    "/home/notes.txt" },
        { "/home",      "./a/./b/",     "/home/a/b" },
        { "/home",      "..",           "/" },
        { "/home",      "../oric",      "/oric" },
        { "/oric/keys", "../../atom",   "/atom" },
        { "/oric/keys", "/",            "/" },
        { "/oric/keys", "//x//y//",     "/x/y" },
        { "/a/b",       "c/../../d",    "/a/d" },
        { "/a",         "...",          "/a/..." },
        { "/a",         ".hidden",      "/a/.hidden" },
        { "/a",         "..x",          "/a/..x" },
        { "/a/b",       "/c/./..",      "/" },
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        const char *got = res(cases[i].cwd, cases[i].arg);
        CHECK(strcmp(got, cases[i].want) == 0, "'%s' in '%s': '%s', want '%s'", cases[i].arg,
              cases[i].cwd, got, cases[i].want);
    }

    /* The limit: SHELL_PATH_MAX fits, one more does not, even when ..
     * would shorten it later. */
    char arg[SHELL_PATH_MAX + 8];
    memset(arg, 'x', sizeof arg);
    arg[0] = '/';
    arg[SHELL_PATH_MAX] = '\0';
    CHECK(path_resolve(out, "/", arg) && strlen(out) == SHELL_PATH_MAX, "the longest path fits");
    arg[SHELL_PATH_MAX] = 'x';
    arg[SHELL_PATH_MAX + 1] = '\0';
    CHECK(!path_resolve(out, "/", arg), "one more is refused");
    CHECK(!path_resolve(out, "/home", arg + 1), "relative, too");

    CHECK(strcmp(path_base("/home/a.txt"), "a.txt") == 0, "base");
    CHECK(strcmp(path_base("/"), "") == 0, "base of the root");

    CHECK(path_join(out, "/", "a") && strcmp(out, "/a") == 0, "join at the root: %s", out);
    CHECK(path_join(out, "/home", "a") && strcmp(out, "/home/a") == 0, "join: %s", out);

    CHECK(path_same("/Home/A.txt", "/home/a.TXT"), "FAT's case");
    CHECK(!path_same("/home/a", "/home/ab"), "a prefix is not the same");
    CHECK(!path_same("/home/ab", "/home/a"), "nor the other way");
    CHECK(path_within("/a/b/c", "/a/b"), "within");
    CHECK(path_within("/A/B", "/a/b"), "within is itself, case aside");
    CHECK(!path_within("/a/bc", "/a/b"), "a sibling with a longer name is not within");
    CHECK(path_within("/x", "/"), "everything is within the root");

    TEST_DONE();
}
