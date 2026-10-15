/* test_files.c — M1's file builtins and redirection over FAT16 and FAT32
 * images (design.md §4.2, §4.5, §10).
 *
 * Each command is run through shell_run, as typed, against an image file
 * FatFs formatted; its output and errors are read as text, and the card's
 * files are read back through FatFs. Then the faults: a card that fails
 * part way through a write, a full card, a missing card, and Ctrl-C. */

#include <stdio.h>
#include <string.h>

#include "files.h"
#include "host_disk.h"
#include "shell.h"
#include "test_util.h"

static shell_t sh;
static char    con[1 << 16];   /* the terminal's text, CR removed */
static size_t  con_len;
static int     interrupt_after = -1;   /* polls before Ctrl-C, or -1 */

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
    if (interrupt_after < 0) return false;
    return interrupt_after-- == 0;
}

/* Run a line; its status, with the terminal's text in con. */
static int run(const char *line) {
    con_len = 0;
    con[0] = '\0';
    return shell_run(&sh, line);
}

/* A file's bytes, through FatFs, or NULL if it cannot be read. */
static const char *slurp(const char *path) {
    static char buf[1 << 17];
    FIL f;
    UINT n;
    if (f_open(&f, path, FA_READ) != FR_OK) return NULL;
    FRESULT r = f_read(&f, buf, sizeof buf - 1, &n);
    f_close(&f);
    if (r != FR_OK) return NULL;
    buf[n] = '\0';
    return buf;
}

static bool exists(const char *path) {
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK;
}

static bool put(const char *path, const char *text) {
    FIL f;
    UINT n;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    FRESULT r = f_write(&f, text, (UINT)strlen(text), &n);
    return f_close(&f) == FR_OK && r == FR_OK && n == strlen(text);
}

/* Some text of `n` bytes that differs at every 512-byte sector. */
static const char *pattern(size_t n, char seed) {
    static char buf[1 << 17];
    for (size_t i = 0; i < n; i++) buf[i] = (char)('a' + (i / 7 + (size_t)seed) % 26);
    for (size_t i = 63; i < n; i += 64) buf[i] = '\n';
    buf[n] = '\0';
    return buf;
}

static int files_tests(bool fat32) {
    const char *kind = fat32 ? "FAT32" : "FAT16";
    CHECK(host_disk_create(NULL, fat32 ? 64u << 20 : 32u << 20, fat32), "%s image", kind);
    files_mount();
    shell_start(&sh);

    /* The starting directory is / without /home, and /home with it. */
    CHECK(strcmp(sh.cwd, "/") == 0, "%s: no /home: '%s'", kind, sh.cwd);
    CHECK(run("mkdir /home") == 0, "mkdir /home: %s", con);
    shell_start(&sh);
    CHECK(strcmp(sh.cwd, "/home") == 0, "%s: start in /home: '%s'", kind, sh.cwd);

    /* df names the type. */
    CHECK(run("df") == 0 && strstr(con, "Type    Size  Used Avail Use%\n") == con &&
              strstr(con, kind),
          "%s: df:\n%s", kind, con);

    /* echo, > and >>, cat, <. Files hold LF, as written, never CR LF. */
    CHECK(run("echo hello > a.txt") == 0 && con_len == 0, "echo >: '%s'", con);
    const char *s = slurp("/home/a.txt");
    CHECK(s && strcmp(s, "hello\n") == 0, "%s: a.txt: '%s'", kind, s ? s : "(none)");
    CHECK(run("echo more 'and more' >> a.txt") == 0, ">>: %s", con);
    s = slurp("/home/a.txt");
    CHECK(s && strcmp(s, "hello\nmore and more\n") == 0, ">>: '%s'", s ? s : "(none)");
    CHECK(!exists("/home/a.txt.new"), "no .new left behind");
    CHECK(run("echo new >> fresh.txt") == 0 && slurp("/home/fresh.txt") &&
              strcmp(slurp("/home/fresh.txt"), "new\n") == 0,
          ">> makes a file");
    CHECK(run("cat a.txt") == 0 && strcmp(con, "hello\nmore and more\n") == 0, "cat: '%s'", con);
    CHECK(run("cat < a.txt") == 0 && strcmp(con, "hello\nmore and more\n") == 0, "cat <: '%s'",
          con);
    CHECK(run("cat a.txt fresh.txt") == 0 && strcmp(con, "hello\nmore and more\nnew\n") == 0,
          "cat two");
    CHECK(run("cat") == 1 && strstr(con, "usage: cat"), "cat with no input: '%s'", con);
    CHECK(run("cat nope") == 1 && strcmp(con, "cat: nope: no such file or directory\n") == 0,
          "cat nope: '%s'", con);
    CHECK(run("cat /home") == 1 && strcmp(con, "cat: /home: is a directory\n") == 0,
          "cat a directory: '%s'", con);
    CHECK(run("cat < nope") == 1 && strstr(con, "nope: no such file"), "< nope: '%s'", con);
    CHECK(run("echo x > /home") == 1 && strstr(con, "is a directory"), "> a dir: '%s'", con);
    CHECK(run("echo x > /nodir/f") == 1 && strstr(con, "no such file"), "> no dir: '%s'", con);
    /* cat a > a reads the old and writes the new through a.new. */
    CHECK(run("cat a.txt fresh.txt > a.txt") == 0, "cat a > a: %s", con);
    s = slurp("/home/a.txt");
    CHECK(s && strcmp(s, "hello\nmore and more\nnew\n") == 0, "cat a > a: '%s'", s ? s : "");
    /* An error goes to the terminal, never into the file. */
    CHECK(run("cat nope > err.txt") == 1 && strstr(con, "nope: no such file"), "err to con");
    s = slurp("/home/err.txt");
    CHECK(s && s[0] == '\0', "the redirection still makes its file, empty: '%s'", s ? s : "");

    /* pwd and cd, with the card's spelling in the prompt. */
    CHECK(run("pwd") == 0 && strcmp(con, "/home\n") == 0, "pwd: '%s'", con);
    CHECK(run("mkdir Docs") == 0, "mkdir Docs");
    CHECK(run("cd DOCS") == 0 && strcmp(sh.cwd, "/home/Docs") == 0, "cd DOCS: '%s'", sh.cwd);
    CHECK(run("cd ..") == 0 && strcmp(sh.cwd, "/home") == 0, "cd ..: '%s'", sh.cwd);
    CHECK(run("cd /") == 0 && strcmp(sh.cwd, "/") == 0, "cd /");
    CHECK(run("cd ../..") == 0 && strcmp(sh.cwd, "/") == 0, "cd .. at the root");
    CHECK(run("cd") == 0 && strcmp(sh.cwd, "/home") == 0, "cd alone: '%s'", sh.cwd);
    CHECK(run("cd nope") == 1 && strcmp(con, "cd: nope: no such file or directory\n") == 0 &&
              strcmp(sh.cwd, "/home") == 0,
          "cd nope: '%s'", con);
    CHECK(run("cd a.txt") == 1 && strcmp(con, "cd: a.txt: not a directory\n") == 0, "cd file");
    CHECK(run("cd a b") == 1 && strstr(con, "usage: cd"), "cd a b");

    /* ls: sorted without regard to case, in columns on the terminal,
     * one per line into a file; hidden names; -l. */
    CHECK(run("mkdir /l && cd /l && touch b C a .hidden ._mac && mkdir D") == 0, "make /l: %s",
          con);
    CHECK(run("ls") == 0 && strcmp(con, "a   b   C   D/\n") == 0, "ls: '%s'", con);
    /* FAT's hidden and system bits hide a name as a leading '.' does. */
    CHECK(run("touch secret sys") == 0 && f_chmod("/l/secret", AM_HID, AM_HID) == FR_OK &&
              f_chmod("/l/sys", AM_SYS, AM_SYS) == FR_OK,
          "hidden and system files");
    CHECK(run("ls") == 0 && strcmp(con, "a   b   C   D/\n") == 0, "hidden by attribute: '%s'", con);
    /* '.' < 'H' < '_' once case is folded. */
    CHECK(run("ls -a") == 0 && strcmp(con, ".hidden  a        C        secret\n"
                                           "._mac    b        D/       sys\n") == 0,
          "ls -a:\n%s", con);
    CHECK(run("ls secret") == 0 && strcmp(con, "secret\n") == 0, "named, it is shown: '%s'", con);
    CHECK(run("ls > /out.txt") == 0, "ls >");
    s = slurp("/out.txt");
    CHECK(s && strcmp(s, "a\nb\nC\nD/\n") == 0, "ls > file: '%s'", s ? s : "");
    CHECK(run("echo abc > a") == 0 && run("ls -l") == 0 &&
              strcmp(con, "-rw        4 a\n-rw        0 b\n-rw        0 C\ndrw          D/\n") == 0,
          "ls -l:\n%s", con);
    CHECK(f_chmod("/l/b", AM_RDO, AM_RDO) == FR_OK, "chmod");
    CHECK(run("ls -l b") == 0 && strcmp(con, "-r-        0 b\n") == 0, "ls -l read-only: '%s'",
          con);
    CHECK(run("ls -la /l/D") == 0 && strcmp(con, "") == 0, "ls an empty dir: '%s'", con);
    CHECK(run("ls a D") == 0 && strcmp(con, "a\n\nD:\n") == 0, "ls a file and a dir: '%s'", con);
    CHECK(run("ls -x") == 1 && strstr(con, "ls: -x: unknown option"), "ls -x: '%s'", con);
    CHECK(run("ls nope") == 1 && strcmp(con, "ls: nope: no such file or directory\n") == 0,
          "ls nope");
    CHECK(run("ls / /home") == 0 && strstr(con, "/:\n") == con && strstr(con, "\n\n/home:\n"),
          "ls headers:\n%s", con);
    /* Columns: down, then across, in the terminal's width. */
    CHECK(run("mkdir /cols && cd /cols") == 0, "cols");
    char line[64];
    for (int i = 0; i < 12; i++) {
        snprintf(line, sizeof line, "touch name%02d.txt", i);
        run(line);
    }
    CHECK(run("ls") == 0 &&
              strcmp(con, "name00.txt  name03.txt  name06.txt  name09.txt\n"
                          "name01.txt  name04.txt  name07.txt  name10.txt\n"
                          "name02.txt  name05.txt  name08.txt  name11.txt\n") != 0,
          "4 columns cannot fit in 40");
    CHECK(strcmp(con, "name00.txt  name04.txt  name08.txt\n"
                      "name01.txt  name05.txt  name09.txt\n"
                      "name02.txt  name06.txt  name10.txt\n"
                      "name03.txt  name07.txt  name11.txt\n") == 0,
          "ls in 40 columns:\n%s", con);
    sh.wide = true;
    CHECK(run("ls") == 0 && strcmp(con, "name00.txt  name03.txt  name06.txt  name09.txt\n"
                                        "name01.txt  name04.txt  name07.txt  name10.txt\n"
                                        "name02.txt  name05.txt  name08.txt  name11.txt\n") ==
                                0,
          "ls in 64 columns:\n%s", con);
    sh.wide = false;

    /* cp: a file, into a directory, several, through .new. */
    CHECK(run("cd /home && cp a.txt b.txt") == 0, "cp: %s", con);
    s = slurp("/home/b.txt");
    CHECK(s && strcmp(s, "hello\nmore and more\nnew\n") == 0, "cp: '%s'", s ? s : "");
    CHECK(run("cp a.txt b.txt fresh.txt Docs") == 0, "cp into a dir: %s", con);
    CHECK(exists("/home/Docs/a.txt") && exists("/home/Docs/b.txt") &&
              exists("/home/Docs/fresh.txt"),
          "cp into a dir");
    CHECK(run("cp fresh.txt a.txt") == 0 && strcmp(slurp("/home/a.txt"), "new\n") == 0,
          "cp over a file");
    CHECK(run("cp a.txt b.txt nope.txt") == 1 && strstr(con, "nope.txt: not a directory"),
          "cp several to a file: '%s'", con);
    CHECK(run("cp a.txt A.TXT") == 1 && strstr(con, "are the same file"), "cp a A: '%s'", con);
    CHECK(run("cp Docs D2") == 1 && strstr(con, "Docs: is a directory"), "cp a dir: '%s'",
          con);
    CHECK(run("cp nope x") == 1 && strstr(con, "nope: no such file"), "cp nope");
    CHECK(run("cp a.txt") == 1 && strstr(con, "usage: cp"), "cp one operand");
    CHECK(put("/home/big.bin", pattern(100000, 1)), "big");
    CHECK(run("cp big.bin big2.bin") == 0 && slurp("/home/big2.bin") &&
              memcmp(slurp("/home/big2.bin"), pattern(100000, 1), 100001) == 0,
          "cp of 100,000 bytes");

    /* cp -r: a tree, made or merged into; never into itself. */
    CHECK(run("mkdir -p t/x/y && echo 1 > t/one && echo 2 > t/x/two && echo 3 > t/x/y/three && "
              "touch t/.dot") == 0,
          "make the tree: %s", con);
    CHECK(run("cp -r t u") == 0, "cp -r: %s", con);
    CHECK(slurp("/home/u/one") && strcmp(slurp("/home/u/one"), "1\n") == 0 &&
              slurp("/home/u/x/two") && strcmp(slurp("/home/u/x/two"), "2\n") == 0 &&
              slurp("/home/u/x/y/three") && strcmp(slurp("/home/u/x/y/three"), "3\n") == 0 &&
              exists("/home/u/.dot"),
          "cp -r copied the tree");
    CHECK(run("cp -r t u") == 0 && exists("/home/u/t/x/y/three"), "cp -r into a dir: %s", con);
    CHECK(run("cp -r t t/x") == 1 && strstr(con, "into itself"), "cp -r into itself: '%s'", con);
    CHECK(run("cp -r t T") == 1 && strstr(con, "into itself"), "cp -r onto itself: '%s'", con);
    CHECK(run("cp -r t a.txt") == 1 && strstr(con, "not a directory"), "cp -r onto a file: '%s'",
          con);
    CHECK(run("cp -r a.txt c.txt") == 0 && exists("/home/c.txt"), "cp -r of a file");

    /* mv: rename, into a directory, over a file, a change of case. */
    CHECK(run("mv c.txt d.txt") == 0 && !exists("/home/c.txt") && exists("/home/d.txt"),
          "mv: %s", con);
    CHECK(run("mv d.txt Docs") == 0 && exists("/home/Docs/d.txt") && !exists("/home/d.txt"),
          "mv into a dir: %s", con);
    CHECK(run("mv fresh.txt b.txt") == 0 && strcmp(slurp("/home/b.txt"), "new\n") == 0 &&
              !exists("/home/fresh.txt"),
          "mv over a file: %s", con);
    CHECK(run("mv b.txt B.TXT") == 0 && run("ls B.txt") == 0, "mv b B: %s", con);
    CHECK(run("ls") == 0 && strstr(con, "B.TXT") && !strstr(con, "b.txt"), "case kept:\n%s",
          con);
    CHECK(run("mv Docs DOCS") == 0 && run("cd docs") == 0 && strcmp(sh.cwd, "/home/DOCS") == 0,
          "mv a dir's case: %s", sh.cwd);
    CHECK(run("cd /home && mv a.txt a.txt") == 1 && strstr(con, "same file"), "mv a a: '%s'",
          con);
    CHECK(run("mv a.txt .") == 1 && strstr(con, "same file"), "mv a .: '%s'", con);
    CHECK(run("mv t t/x") == 1 && strstr(con, "into itself"), "mv into itself: '%s'", con);
    CHECK(run("mv t a.txt") == 1 && strstr(con, "file exists"), "mv a dir onto a file: '%s'", con);
    CHECK(run("mv a.txt u") == 0 && exists("/home/u/a.txt"), "mv a file into a dir");
    CHECK(run("mkdir v && mv v u/x") == 0 && exists("/home/u/x/v"), "mv a dir into a dir");
    CHECK(run("mkdir u2 && touch u2/x && mv u2/x u") == 1 && strstr(con, "is a directory"),
          "mv a file onto a dir: '%s'", con);
    CHECK(run("mv /home /h") == 0 && run("mv /h /home") == 0, "mv the cwd and back: %s", con);
    CHECK(run("mv nope x") == 1 && strstr(con, "nope: no such file"), "mv nope");
    CHECK(run("mv / x") == 1 && strstr(con, "cannot move /"), "mv /");

    /* rm, rm -r, rmdir. */
    CHECK(run("rm B.TXT") == 0 && !exists("/home/B.TXT"), "rm: %s", con);
    CHECK(run("rm nope") == 1 && strcmp(con, "rm: nope: no such file or directory\n") == 0,
          "rm nope");
    CHECK(run("rm t") == 1 && strstr(con, "is a directory"), "rm a dir: '%s'", con);
    CHECK(run("rm -r t") == 0 && !exists("/home/t"), "rm -r: %s", con);
    CHECK(run("rm -r /") == 1 && strstr(con, "refusing"), "rm -r /");
    CHECK(run("rm /l/b") == 1 && strcmp(con, "rm: /l/b: read-only\n") == 0, "rm read-only: '%s'",
          con);
    CHECK(run("echo x > /l/b") == 1 && strstr(con, "read-only"), "> read-only: '%s'", con);
    CHECK(run("cp big.bin /l/b") == 1 && strstr(con, "read-only"), "cp onto read-only: '%s'",
          con);
    CHECK(run("rm -r /l") == 1 && strstr(con, "rm: /l/b: read-only") && exists("/l/b") &&
              !exists("/l/a") && !exists("/l/D"),
          "rm -r keeps going past a read-only file: '%s'", con);
    CHECK(f_chmod("/l/b", 0, AM_RDO) == FR_OK && run("rm -r /l") == 0 && !exists("/l"),
          "rm -r once writable: %s", con);
    CHECK(run("rmdir u") == 1 && strstr(con, "directory not empty"), "rmdir: '%s'", con);
    CHECK(run("rmdir big.bin") == 1 && strstr(con, "not a directory"), "rmdir a file");
    CHECK(run("rm u2/x && rmdir u2") == 0 && !exists("/home/u2"), "rmdir: %s", con);
    CHECK(run("rmdir /") == 1, "rmdir /");

    /* mkdir, mkdir -p, touch. */
    CHECK(run("mkdir u") == 1 && strcmp(con, "mkdir: u: file exists\n") == 0, "mkdir u: '%s'",
          con);
    CHECK(run("mkdir n/o") == 1 && strstr(con, "no such file"), "mkdir n/o: '%s'", con);
    CHECK(run("mkdir -p n/o/p") == 0 && exists("/home/n/o/p"), "mkdir -p: %s", con);
    CHECK(run("mkdir -p n/o/p") == 0, "mkdir -p again");
    CHECK(run("mkdir -p big.bin/x") == 1 && strstr(con, "not a directory"), "mkdir -p file/x");
    CHECK(run("touch big.bin new.txt") == 0 && slurp("/home/new.txt") &&
              slurp("/home/new.txt")[0] == '\0' &&
              memcmp(slurp("/home/big.bin"), pattern(100000, 1), 100001) == 0,
          "touch makes one, leaves one");

    /* ;, && and ||, and a mistake anywhere stops the line. */
    CHECK(run("cat nope && echo yes || echo no") == 0 && strstr(con, "\nno\n") && !strstr(con, "yes"),
          "&& ||: '%s'", con);
    CHECK(run("echo a; echo b") == 0 && strcmp(con, "a\nb\n") == 0, ";");
    CHECK(run("echo a; echo \"b") == 2 && strcmp(con, "shell: unterminated \"\n") == 0,
          "a parse error runs nothing: '%s'", con);
    CHECK(run("nope") == 127 && strcmp(con, "nope: not found\n") == 0, "not found");
    CHECK(run("echo -n a; echo b") == 0 && strcmp(con, "ab\n") == 0, "echo -n");
    /* A line longer than one typed can be is refused, not overrun. */
    static char longline[4 * SHELL_LINE_MAX];
    memset(longline, 'x', sizeof longline - 1);
    CHECK(run(longline) == 2 && strcmp(con, "shell: line too long\n") == 0, "long line: '%s'",
          con);

    /* The lock: cp writing the file a redirection is writing. */
    CHECK(run("cp big.bin new.txt > new.txt") == 1 && strstr(con, "file in use"),
          "a file written twice: '%s'", con);

    /* Ctrl-C: cp stops, and nothing half-copied is put in place. */
    interrupt_after = 2;
    CHECK(run("cp big.bin big3.bin; echo after") == 130 && strstr(con, "^C") &&
              !strstr(con, "after"),
          "cp stopped: '%s'", con);
    interrupt_after = -1;
    CHECK(!exists("/home/big3.bin") && !exists("/home/big3.bin.new"), "no part copy");

    /* More names than ls sorts: all of them, unsorted, and a note. */
    CHECK(run("mkdir /many") == 0, "many");
    for (unsigned i = 0; i <= SHELL_LS_MAX; i++) {
        snprintf(line, sizeof line, "/many/f%u", i);
        CHECK(put(line, ""), "put %s", line);
    }
    CHECK(run("ls /many > /many.txt") == 0 && strstr(con, "not sorted"), "many: '%s'", con);
    s = slurp("/many.txt");
    size_t lines = 0;
    for (const char *q = s; q && *q; q++) lines += *q == '\n';
    CHECK(lines == SHELL_LS_MAX + 1u, "all %u names listed: %zu", SHELL_LS_MAX + 1u, lines);
    CHECK(run("rm -r /many") == 0 && !exists("/many"), "rm -r many");

    /* Deeper than SHELL_TREE_DEPTH: said, not overrun. */
    char deep[8 + 2 * SHELL_TREE_DEPTH] = "/deep";
    for (unsigned i = 0; i <= SHELL_TREE_DEPTH; i++) strcat(deep, "/d");
    char cmd[sizeof deep + 16];
    snprintf(cmd, sizeof cmd, "mkdir -p %s", deep);
    CHECK(run(cmd) == 0, "deep: %s", con);
    CHECK(run("cp -r /deep /deep2") == 1 && strstr(con, "levels deep"), "cp -r deep: '%s'", con);
    CHECK(run("rm -r /deep") == 1 && strstr(con, "levels deep"), "rm -r deep: '%s'", con);

    host_disk_close();
    return test_failures;
}

/* A card that fails part way through replacing a file, at every sector
 * of the way: afterwards the old file is whole, or the new one is, or
 * the old is gone and a whole name.new is there (EL §8.6). */
static int fault_tests(void) {
    static char old_text[5001], new_text[9001];
    memcpy(old_text, pattern(5000, 3), sizeof old_text);
    memcpy(new_text, pattern(9000, 5), sizeof new_text);
    unsigned long writes = 0;
    for (long fail = 0; fail < 1000; fail++) {
        CHECK(host_disk_create(NULL, 32u << 20, false), "image");
        files_mount();
        shell_start(&sh);
        CHECK(put("/a", old_text) && put("/b", new_text), "files");
        unsigned long before = host_disk_writes();
        host_disk_fail_writes_after(fail);
        int status = run("cp /b /a");
        host_disk_fail_writes_after(-1);
        writes = host_disk_writes() - before;
        files_mount();   /* as after the card was taken out and put back */
        const char *a = slurp("/a");
        bool old_whole = a && strcmp(a, old_text) == 0;
        bool new_whole = a && strcmp(a, new_text) == 0;
        const char *n = a ? NULL : slurp("/a.new");
        bool recoverable = !a && n && strcmp(n, new_text) == 0;
        CHECK(old_whole || new_whole || recoverable, "failing after %ld writes: a is %s", fail,
              a ? "neither" : "missing, and a.new not whole");
        CHECK(status != 0 || new_whole, "after %ld: success said, but a is not new", fail);
        CHECK(status == 0 || strstr(con, "cp: "), "after %ld: a failure not said", fail);
        host_disk_close();
        if (status == 0) break;   /* every write got through */
    }
    CHECK(writes > 10, "the copy took %lu writes", writes);

    /* A full card: the copy fails, says so, and the old file stays. */
    CHECK(host_disk_create(NULL, 32u << 20, false), "image");
    files_mount();
    shell_start(&sh);
    CHECK(put("/a", old_text) && put("/b", new_text), "files");
    FIL f;
    CHECK(f_open(&f, "/fill", FA_CREATE_ALWAYS | FA_WRITE) == FR_OK, "fill");
    for (;;) {
        UINT n;
        if (f_write(&f, pattern(4096, 0), 4096, &n) != FR_OK || n < 4096) break;
    }
    f_close(&f);
    CHECK(run("df") == 0 && strstr(con, " 100%\n") && strstr(con, " 0.0K 100%"),
          "df of a full card:\n%s", con);
    CHECK(run("cp /b /a") == 1 && strstr(con, "no space left on the card"), "full: '%s'", con);
    CHECK(slurp("/a") && strcmp(slurp("/a"), old_text) == 0 && !exists("/a.new"),
          "the old file stays");
    CHECK(run("echo x >> /a") == 1 && strstr(con, "no space"), ">> full: '%s'", con);
    CHECK(slurp("/a") && strcmp(slurp("/a"), old_text) == 0, ">> keeps the old file");
    host_disk_close();

    /* No card: each command says so, and the shell carries on. */
    files_mount();
    shell_start(&sh);
    CHECK(strcmp(sh.cwd, "/") == 0, "no card: start at /");
    CHECK(run("ls") == 1 && strcmp(con, "ls: .: no card\n") == 0, "ls with no card: '%s'", con);
    CHECK(run("cd /home") == 1 && strstr(con, "no card"), "cd with no card");
    CHECK(run("echo x > /a") == 1 && strstr(con, "no card"), "> with no card: '%s'", con);
    CHECK(run("df") == 1 && strstr(con, "no card"), "df with no card");
    CHECK(run("echo still here") == 0 && strcmp(con, "still here\n") == 0, "echo needs no card");

    return test_failures;
}

int main(void) {
    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now, interrupted, NULL, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");

    if (files_tests(false)) return 1;
    if (files_tests(true)) return 1;
    if (fault_tests()) return 1;
    TEST_DONE();
}
