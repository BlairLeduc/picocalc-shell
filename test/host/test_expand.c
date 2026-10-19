/* test_expand.c — words as a command runs: quotes, variables and globs
 * (expand.h, vars.h, path_match; design.md §4.4).
 *
 * The words are shown by echo, through shell_run; globs over FAT16 and
 * FAT32 images. */

#include <stdio.h>
#include <string.h>

#include "files.h"
#include "host_disk.h"
#include "path.h"
#include "shell.h"
#include "test_util.h"
#include "vars.h"

static shell_t sh;
static char    con[1 << 15];
static size_t  con_len;

static void sink(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    for (size_t i = 0; i < n; i++)
        if (buf[i] != '\r' && con_len + 1 < sizeof con) con[con_len++] = buf[i];
    con[con_len] = '\0';
}

static uint64_t now(void *ctx) {
    (void)ctx;
    return 0;
}

static bool interrupted(void *ctx) {
    (void)ctx;
    return false;
}

static int run(const char *line) {
    con_len = 0;
    con[0] = '\0';
    return shell_run(&sh, line);
}

static bool touch(const char *path) {
    FIL f;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    return f_close(&f) == FR_OK;
}

static int glob_tests(bool fat32) {
    const char *kind = fat32 ? "FAT32" : "FAT16";
    CHECK(host_disk_create(NULL, fat32 ? 64u << 20 : 32u << 20, fat32), "%s image", kind);
    files_mount();
    shell_start(&sh);
    CHECK(f_mkdir("/g") == FR_OK && f_mkdir("/g/sub") == FR_OK && touch("/g/b.txt") &&
              touch("/g/A.txt") && touch("/g/c.TXT") && touch("/g/notes") &&
              touch("/g/.hid.txt") && touch("/g/._mac.txt") && touch("/g/x[1]") &&
              touch("/g/sub/deep.txt") && touch("/g/sys.txt") &&
              f_chmod("/g/sys.txt", AM_HID, AM_HID) == FR_OK,
          "%s: files", kind);
    CHECK(run("cd /g") == 0, "cd /g");
    /* $HOME is cd's default. */
    CHECK(run("HOME=/g/sub; cd; pwd") == 0 && strcmp(con, "/g/sub\n") == 0, "cd $HOME: '%s'", con);
    CHECK(run("unset HOME; cd") == 1 && strstr(con, "HOME is not set"), "no HOME: '%s'", con);
    CHECK(run("HOME=/home; cd /g") == 0, "cd /g");

    /* Sorted as ls sorts, case folded; the case of the card kept. */
    CHECK(run("echo *.txt") == 0 && strcmp(con, "A.txt b.txt c.TXT\n") == 0, "%s: *.txt: '%s'",
          kind, con);
    CHECK(run("echo ?.txt") == 0 && strcmp(con, "A.txt b.txt c.TXT\n") == 0, "?.txt: '%s'", con);
    CHECK(run("echo [ab].*") == 0 && strcmp(con, "A.txt b.txt\n") == 0, "[ab].*: '%s'", con);
    CHECK(run("echo [!ab].txt") == 0 && strcmp(con, "c.TXT\n") == 0, "[!ab]: '%s'", con);
    CHECK(run("echo [a-b]*") == 0 && strcmp(con, "A.txt b.txt\n") == 0, "range: '%s'", con);
    CHECK(run("echo *") == 0 && strcmp(con, "A.txt b.txt c.TXT notes sub x[1]\n") == 0,
          "*: hidden names left out: '%s'", con);
    CHECK(run("echo .*") == 0 && strcmp(con, ".hid.txt ._mac.txt\n") == 0, ".*: '%s'", con);
    /* In another directory, given as typed. */
    CHECK(run("echo sub/*") == 0 && strcmp(con, "sub/deep.txt\n") == 0, "sub/*: '%s'", con);
    CHECK(run("echo /g/sub/d*") == 0 && strcmp(con, "/g/sub/deep.txt\n") == 0, "abs: '%s'", con);
    CHECK(run("echo ../g/n*") == 0 && strcmp(con, "../g/notes\n") == 0, "../: '%s'", con);
    /* No match, quoted, escaped, or in a directory's name: left as is. */
    CHECK(run("echo *.none") == 0 && strcmp(con, "*.none\n") == 0, "no match: '%s'", con);
    CHECK(run("echo '*.txt' \"*.txt\" \\*.txt") == 0 && strcmp(con, "*.txt *.txt *.txt\n") == 0,
          "quoted: '%s'", con);
    CHECK(run("echo s*/deep.txt") == 0 && strcmp(con, "s*/deep.txt\n") == 0, "dir glob: '%s'",
          con);
    /* A quoted part of a glob is literal. */
    CHECK(run("echo x'['*") == 0 && strcmp(con, "x[1]\n") == 0, "x'['*: '%s'", con);
    CHECK(run("echo 'x['1]") == 0 && strcmp(con, "x[1]\n") == 0, "no glob: '%s'", con);
    /* A variable's value is not globbed. */
    CHECK(run("P='*.txt'; echo $P") == 0 && strcmp(con, "*.txt\n") == 0, "$P: '%s'", con);
    /* Commands see the matches as their operands. */
    CHECK(run("rm *.txt && ls") == 0 && strcmp(con, "notes  sub/   x[1]\n") == 0, "rm *: '%s'",
          con);
    CHECK(run("cat *.none") == 1 && strcmp(con, "cat: *.none: invalid name\n") == 0,
          "no match is an operand: '%s'", con);

    /* Too many matches for SHELL_ARGS_MAX: refused, not cut short. */
    char name[32];
    for (unsigned i = 0; i <= SHELL_ARGS_MAX; i++) {
        snprintf(name, sizeof name, "/g/sub/f%03u", i);
        CHECK(touch(name), "%s", name);
    }
    CHECK(run("echo sub/f*") == 1 && strcmp(con, "shell: too many arguments\n") == 0,
          "too many: '%s'", con);
    host_disk_close();
    return test_failures;
}

int main(void) {
    /* path_match on its own. */
    static const struct { const char *pat, *name; bool want; } m[] = {
        { "*",        "",         true },
        { "*",        "abc",      true },
        { "a*",       "ABC",      true },
        { "*c",       "abc",      true },
        { "*b*",      "abc",      true },
        { "*x*",      "abc",      false },
        { "a?c",      "abc",      true },
        { "a?c",      "ac",       false },
        { "??",       "a",        false },
        { "*.txt",    "a.txt.bak", false },
        { "*.*.bak",  "a.txt.bak", true },
        { "[abc]",    "B",        true },
        { "[a-c]x",   "cx",       true },
        { "[!a-c]x",  "cx",       false },
        { "[^a-c]x",  "dx",       true },
        { "[]]",      "]",        true },
        { "[a-]",     "-",        true },
        { "[",        "[",        true },
        { "[ab",      "[ab",      true },
        { "\\*",      "*",        true },
        { "\\*",      "a",        false },
        { "a\\?",     "a?",       true },
        { "*a*a*a*b", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", false },
        { "x[\\]]",   "x]",       true },
    };
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++)
        CHECK(path_match(m[i].pat, m[i].name) == m[i].want, "'%s' on '%s'", m[i].pat, m[i].name);

    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now, interrupted, NULL, NULL, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");

    /* Quotes and escapes, as the parser's tests had them before M4. */
    static const struct { const char *line, *want; } q[] = {
        { "echo 'a  b' \"c d\"",       "a  b c d\n" },
        { "echo it\\'s",               "it's\n" },
        { "echo a\\ b",                "a b\n" },
        { "echo 'x'\"y\"z",            "xyz\n" },
        { "echo \"a\\\"b\\\\c\\d\"",   "a\"b\\c\\d\n" },
        { "echo '\\'",                 "\\\n" },
        { "echo '#' \\#",              "# #\n" },
        { "echo '>' \\; \"&&\" '|'",   "> ; && |\n" },
        { "echo '$HOME' \\$HOME \"\\$HOME\"", "$HOME $HOME $HOME\n" },
        { "echo $HOME \"$HOME\" ${HOME}x", "/home /home /homex\n" },
        { "echo $ a$ $1 ${ ${1x} $-",  "$ a$ $1 ${ ${1x} $-\n" },
        { "echo [$NOPE] \"[$NOPE]\"",  "[] []\n" },
        { "echo $NOPE '' \"\" x",      "  x\n" },
        { "echo $PATH",                "/pico2-apps\n" },
    };
    for (size_t i = 0; i < sizeof q / sizeof q[0]; i++)
        CHECK(run(q[i].line) == 0 && strcmp(con, q[i].want) == 0, "%s gave '%s', want '%s'",
              q[i].line, con, q[i].want);

    /* An empty word with no quotes is dropped: echo gets no operand. */
    CHECK(run("echo $NOPE") == 0 && strcmp(con, "\n") == 0, "$NOPE: '%s'", con);
    CHECK(run("$NOPE") == 0 && con_len == 0, "a command of nothing: '%s'", con);

    /* $? is the last command's status, as each command runs. */
    CHECK(run("cat; echo $?") == 0 && strstr(con, "\n1\n"), "$? after cat: '%s'", con);
    CHECK(run("nosuch; echo $? $?") == 0 && strstr(con, "127 127\n"), "$? 127: '%s'", con);
    CHECK(run("echo x; echo $?") == 0 && strcmp(con, "x\n0\n") == 0, "$? 0: '%s'", con);

    /* Assignment, set, unset, export. */
    CHECK(run("A=1 B='two words' C=\"$A$A\"; echo $A $B $C") == 0 &&
              strcmp(con, "1 two words 11\n") == 0,
          "assign: '%s'", con);
    CHECK(run("A=x$A; echo $A") == 0 && strcmp(con, "x1\n") == 0, "A=x$A: '%s'", con);
    CHECK(run("set") == 0 && strcmp(con, "HOME=/home\nPATH=/pico2-apps\nB=two words\nC=11\nA=x1\n") == 0,
          "set: '%s'", con);
    CHECK(run("unset A B; echo [$A$B]") == 0 && strcmp(con, "[]\n") == 0, "unset: '%s'", con);
    CHECK(run("export D=4 E; echo $D") == 0 && strcmp(con, "4\n") == 0, "export: '%s'", con);
    CHECK(run("export 1x=2") == 1 && strstr(con, "not a name"), "export 1x: '%s'", con);
    CHECK(run("unset 1x") == 1 && strstr(con, "not a name"), "unset 1x: '%s'", con);
    CHECK(run("A=1 echo hi") == 1 && strstr(con, "not supported") && !vars_get("A", 1),
          "A=1 cmd: '%s'", con);
    CHECK(run("A=1 > f") == 1 && strstr(con, "with a redirection is not supported") &&
              !vars_get("A", 1),
          "A=1 > f: '%s'", con);
    CHECK(run("A=1 < f") == 1 && !vars_get("A", 1), "A=1 < f: '%s'", con);
    CHECK(run("X=1=2; echo $X") == 0 && strcmp(con, "1=2\n") == 0, "X=1=2: '%s'", con);
    CHECK(run("'A'=1") == 127, "a quoted name is not an assignment: '%s'", con);
    /* The table fills, and a set that does not fit keeps the old value. */
    char line[SHELL_LINE_MAX + 1];
    memset(line, 0, sizeof line);
    strcpy(line, "BIG=");
    memset(line + 4, 'v', 200);
    int ok = 0;
    for (int i = 0; i < 10; i++) {
        line[1] = (char)('A' + i);
        if (run(line) == 0) ok++;
    }
    CHECK(ok > 0 && ok < 10 && strstr(con, "no room"), "full: %d set, '%s'", ok, con);
    CHECK(run("D=5; echo $D") == 0 && strcmp(con, "5\n") == 0, "a small one still fits: '%s'",
          con);
    for (int i = 0; i < 10; i++) {
        char u[16];
        snprintf(u, sizeof u, "unset B%cG", 'A' + i);
        run(u);
    }
    /* An empty redirection is refused. */
    CHECK(run("echo x > $NOPE") == 1 && strstr(con, "no file"), "> $NOPE: '%s'", con);

    if (glob_tests(false)) return 1;
    if (glob_tests(true)) return 1;
    TEST_DONE();
}
