/* test_dos.c — the DOS flavour's command line (design.md §4.7, M9 step
 * 1): its paths, wildcards and parser on their own, then its commands
 * run as typed over FAT16 and FAT32 images, as test_files does for the
 * UNIX flavour's. Linked with shell_core_dos.
 *
 * Output and errors are read as text from the terminal's sink, the
 * card's files back through FatFs, and the keys a command waits for
 * (DEL's question, DIR /P's pause) come from a script. */

#include <stdio.h>
#include <string.h>

#include "dos.h"
#include "files.h"
#include "flavour.h"
#include "history.h"
#include "host_disk.h"
#include "keycodes.h"
#include "run.h"
#include "shell.h"
#include "test_util.h"
#include "vars.h"

static shell_t sh;
static char    con[1 << 16];   /* the terminal's text, CR removed */
static size_t  con_len;
static const char *keys = "";  /* what getkey types; -1 after it */

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

static int getkey(void *ctx) {
    (void)ctx;
    return *keys ? (unsigned char)*keys++ : -1;
}

static void clear(void) {
    con_len = 0;
    con[0] = '\0';
}

/* Run a line; its status, with the terminal's text in con. */
static int run(const char *line) {
    clear();
    return shell_run(&sh, line);
}

static bool has(const char *text) {
    return strstr(con, text) != NULL;
}

static const char *slurp(const char *path) {
    static char buf[1 << 15];
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

static BYTE attrs(const char *path) {
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK ? fno.fattrib : 0xFF;
}

static bool put(const char *path, const char *text) {
    FIL f;
    UINT n;
    if (f_open(&f, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    FRESULT r = f_write(&f, text, (UINT)strlen(text), &n);
    return f_close(&f) == FR_OK && r == FR_OK && n == strlen(text);
}

/* ---- on their own ----------------------------------------------------- */

static int unit_tests(void) {
    static const struct {
        const char *pat, *name;
        bool hit;
    } m[] = {
        { "*.*", "README", true },      { "*.*", "a.b.c", true },
        { "*.", "README", true },       { "*.", "A.TXT", false },
        { "*.TXT", "notes.txt", true }, { "*.TXT", "TXT", false },
        { "A?.TXT", "A.TXT", true },    { "A?.TXT", "AB.TXT", true },
        { "A?.TXT", "ABC.TXT", false }, { "*", "x.y", true },
        { "R*E", "readme", true },      { "R*E", "readmes", false },
        { "pico-*.cfg", "pico-atom.cfg", true },
        { "*.gz", "archive.tar.gz", true },
        { "FOO", "foo", true },         { "FOO", "foo.txt", false },
        { "FOO.*", "foo", true },
    };
    for (unsigned i = 0; i < sizeof m / sizeof m[0]; i++)
        CHECK(dos_match(m[i].pat, m[i].name) == m[i].hit, "match %s %s", m[i].pat, m[i].name);

    static const struct {
        const char *mask, *name, *want;
    } rn[] = {
        { "*.BAK", "README.TXT", "README.BAK" }, { "X*.*", "foo.c", "Xoo.c" },
        { "?A*", "abc", "aAc" },                 { "*.", "a.txt", "a" },
        { "NEW.TXT", "old.txt", "NEW.TXT" },     { "*.OLD", "noext", "noext.OLD" },
        { "??.*", "abcd.e", "ab.e" },
    };
    for (unsigned i = 0; i < sizeof rn / sizeof rn[0]; i++) {
        char out[64];
        CHECK(dos_rename(out, sizeof out, rn[i].mask, rn[i].name) && strcmp(out, rn[i].want) == 0,
              "rename %s %s: '%s'", rn[i].mask, rn[i].name, out);
    }

    static const struct {
        const char *arg, *want;
    } rs[] = {
        { "C:\\X", "/X" },   { "..\\Y", "/Y" },       { "C:Z", "/HOME/Z" },
        { "/A/B", "/A/B" },  { "c:", "/HOME" },        { "C:\\", "/" },
        { "a\\.\\b\\..", "/HOME/a" }, { "\\", "/" },
    };
    for (unsigned i = 0; i < sizeof rs / sizeof rs[0]; i++) {
        char out[SHELL_PATH_MAX + 1];
        CHECK(dos_resolve(out, "/HOME", rs[i].arg) && strcmp(out, rs[i].want) == 0,
              "resolve %s: '%s'", rs[i].arg, out);
    }
    char out[SHELL_PATH_MAX + 1];
    CHECK(!dos_resolve(out, "/", "A:\\X") && dos_other_drive("b:") && !dos_other_drive("C:"),
          "other drives");
    char shown[64];
    dos_show(shown, sizeof shown, "/");
    CHECK(strcmp(shown, "C:\\") == 0, "root shown '%s'", shown);
    dos_show(shown, sizeof shown, "/A/b");
    CHECK(strcmp(shown, "C:\\A\\b") == 0, "path shown '%s'", shown);

    vars_set("FOO", 3, "bar");
    char line[64];
    CHECK(dos_expand(line, sizeof line, "%foo%-%%-%NOPE%-50% %") &&
              strcmp(line, "bar-%--50% %") == 0,
          "expand '%s'", line);
    CHECK(!dos_expand(line, 8, "%FOO%%FOO%%FOO%"), "expansion too long");

    static dos_parse_t p;
    CHECK(dos_parse(&p, "DIR/W/P") && p.ncmds == 1 && p.cmds[0].argc == 2 &&
              strcmp(p.cmds[0].argv[0], "DIR") == 0 && strcmp(p.cmds[0].argv[1], "/W/P") == 0,
          "DIR/W/P");
    CHECK(dos_parse(&p, "cd..") && p.cmds[0].argc == 2 && strcmp(p.cmds[0].argv[0], "cd") == 0 &&
              strcmp(p.cmds[0].argv[1], "..") == 0,
          "cd..");
    CHECK(dos_parse(&p, "CD\\") && p.cmds[0].argc == 2 && strcmp(p.cmds[0].argv[1], "\\") == 0,
          "CD\\");
    CHECK(dos_parse(&p, "ECHO.") && p.cmds[0].argc == 2 && strcmp(p.cmds[0].tail, ".") == 0,
          "ECHO.");
    CHECK(dos_parse(&p, "pico-atom") && strcmp(p.cmds[0].argv[0], "pico-atom") == 0, "a program");
    CHECK(dos_parse(&p, "FOO.BAT x") && strcmp(p.cmds[0].argv[0], "FOO.BAT") == 0,
          "not a builtin, not cut");
    CHECK(dos_parse(&p, "ECHO  a   b > f.txt") && strcmp(p.cmds[0].tail, "  a   b") == 0 &&
              strcmp(p.cmds[0].out, "f.txt") == 0 && !p.cmds[0].append,
          "tail '%s'", p.cmds[0].tail);
    CHECK(dos_parse(&p, "TYPE \"my file.txt\">>\"o u t\"") && p.cmds[0].argc == 2 &&
              strcmp(p.cmds[0].argv[1], "my file.txt") == 0 &&
              strcmp(p.cmds[0].out, "o u t") == 0 && p.cmds[0].append,
          "quotes");
    CHECK(dos_parse(&p, "SORT<in.txt|FIND \"a|b\" | MORE") && p.ncmds == 3 &&
              strcmp(p.cmds[0].in, "in.txt") == 0 && strcmp(p.cmds[1].argv[1], "a|b") == 0,
          "pipes");
    CHECK(dos_parse(&p, "   ") && p.ncmds == 0, "blank");
    CHECK(!dos_parse(&p, "| X") && !dos_parse(&p, "X |") && !dos_parse(&p, "X >") &&
              !dos_parse(&p, "> f"),
          "syntax errors");
    return 0;
}

/* ---- run as typed ----------------------------------------------------- */

static int dos_tests(bool fat32) {
    const char *fs = fat32 ? "FAT32" : "FAT16";
    CHECK(host_disk_create(NULL, fat32 ? 64u << 20 : 32u << 20, fat32), "%s image", fs);
    files_mount();
    CHECK(f_mkdir("/HOME") == FR_OK && f_mkdir("/pico2-apps") == FR_OK, "dirs");
    CHECK(f_setlabel("PICOCALC") == FR_OK, "label");

    /* The banner and the prompt, $P$G, as the card spells HOME. */
    clear();
    shell_start(&sh);
    CHECK(has("PicoCalc shell\033[0m test (DOS)\n\nC:\\HOME>"), "%s: start '%s'", fs, con);
    CHECK(strcmp(sh.cwd, "/HOME") == 0, "cwd %s", sh.cwd);

    /* Names and switches in either case; CD alone shows where. */
    CHECK(run("cd") == 0 && strcmp(con, "C:\\HOME\n") == 0, "cd: '%s'", con);
    CHECK(run("Md DOCS") == 0 && run("cD docs") == 0 && strcmp(sh.cwd, "/HOME/DOCS") == 0,
          "md, cd: %s", sh.cwd);
    CHECK(run("CD..") == 0 && strcmp(sh.cwd, "/HOME") == 0, "cd..");
    CHECK(run("CD\\") == 0 && strcmp(sh.cwd, "/") == 0, "cd\\");
    CHECK(run("CD C:\\HOME\\DOCS") == 0 && strcmp(sh.cwd, "/HOME/DOCS") == 0, "cd C:");
    CHECK(run("CD /HOME") == 0 && strcmp(sh.cwd, "/HOME") == 0, "cd with /");
    CHECK(run("CD NOPE") == 1 && strcmp(con, "Invalid directory\n") == 0, "'%s'", con);
    CHECK(run("MD DOCS") == 1 && strcmp(con, "Unable to create directory\n") == 0, "'%s'", con);
    CHECK(run("A:") == 1 && strcmp(con, "Invalid drive specification\n") == 0, "'%s'", con);
    CHECK(run("C:") == 0 && strcmp(con, "") == 0, "C:");
    CHECK(run("NOPE") == 127 && strcmp(con, "Bad command or file name\n") == 0, "'%s'", con);

    /* ECHO, redirection and TYPE. */
    CHECK(run("ECHO hello  world>A.TXT") == 0 && run("ECHO.>>a.txt") == 0 &&
              run("echo second line >> A.TXT") == 0,
          "echo");
    CHECK(strcmp(slurp("/HOME/A.TXT"), "hello  world\n\nsecond line\n") == 0, "'%s'",
          slurp("/HOME/A.TXT"));
    CHECK(run("TYPE a.txt") == 0 && strcmp(con, "hello  world\n\nsecond line\n") == 0, "'%s'",
          con);
    CHECK(run("TYPE NOPE.TXT") == 1 && strcmp(con, "File not found - NOPE.TXT\n") == 0, "'%s'",
          con);
    CHECK(run("TYPE DOCS") == 1 && strcmp(con, "Access denied - DOCS\n") == 0, "'%s'", con);
    CHECK(run("TYPE A.TXT > NUL") == 0 && strcmp(con, "") == 0 && !exists("/HOME/NUL"), "> NUL");
    CHECK(run("ECHO") == 0 && strcmp(con, "ECHO is on\n") == 0, "'%s'", con);

    /* COPY. */
    CHECK(run("COPY A.TXT B.TXT") == 0 && strcmp(con, "        1 file(s) copied\n") == 0, "'%s'",
          con);
    CHECK(strcmp(slurp("/HOME/B.TXT"), "hello  world\n\nsecond line\n") == 0, "copied");
    CHECK(run("COPY *.TXT *.BAK") == 0 && has("A.TXT\nB.TXT\n        2 file(s) copied"), "'%s'",
          con);
    CHECK(exists("/HOME/A.BAK") && exists("/HOME/B.BAK"), "masked copies");
    CHECK(run("COPY *.BAK DOCS") == 0 && exists("/HOME/DOCS/A.BAK") && exists("/HOME/DOCS/B.BAK"),
          "into a directory: '%s'", con);
    CHECK(run("COPY A.TXT") == 1 && has("File cannot be copied onto itself") &&
              has("0 file(s) copied"),
          "onto itself: '%s'", con);
    CHECK(run("COPY *.TXT ALL.TXT") == 1 && has("Cannot copy several") && !exists("/HOME/ALL.TXT"),
          "several into one: '%s'", con);
    CHECK(run("COPY NOPE.* DOCS") == 1 && has("File not found - NOPE.*"), "'%s'", con);
    CHECK(run("COPY DOCS C:\\") == 0 && exists("/A.BAK") && exists("/B.BAK"), "a dir's files");

    /* DIR. */
    CHECK(run("DIR") == 0, "dir: '%s'", con);
    CHECK(has(" Volume in drive C is PICOCALC\n Volume Serial Number is ") &&
              has(" Directory of C:\\HOME\n\n"),
          "header: '%s'", con);
    CHECK(has("\nA        TXT         26\n") && has("\nDOCS         <DIR>\n") &&
              has("        4 file(s)        104 bytes\n") && has(" bytes free\n"),
          "lines: '%s'", con);
    CHECK(run("dir *.bak /b") == 0 && strcmp(con, "A.BAK\nB.BAK\n") == 0, "/B: '%s'", con);
    CHECK(run("DIR A /B") == 0 && strcmp(con, "A.TXT\nA.BAK\n") == 0, "NAME is NAME.*: '%s'", con);
    /* In the card's order, as DOS listed them. */
    CHECK(run("DIR/W") == 0 && has("[DOCS]       A.TXT        B.TXT\nA.BAK        B.BAK"),
          "/W: '%s'", con);
    CHECK(run("DIR NOPE") == 1 && has("File not found\n"), "'%s'", con);
    CHECK(run("DIR /X") == 1 && strcmp(con, "Invalid switch - /X\n") == 0, "'%s'", con);
    CHECK(put("/HOME/.hidden", "h") && put("/HOME/._A.TXT", "x"), "hidden");
    CHECK(run("DIR /B") == 0 && !has(".hidden") && run("DIR /B /A") == 0 && has(".hidden"),
          "/A: '%s'", con);
    CHECK(run("DIR \\ /S /B") == 0 && has("C:\\HOME\\DOCS\\A.BAK\n") && has("C:\\A.BAK\n") &&
              !has("C:\\\\"),
          "/S /B: '%s'", con);
    CHECK(run("DIR /S") == 0 && has(" Directory of C:\\HOME\\DOCS\n") &&
              has("Total files listed:\n        6 file(s) "),
          "/S: '%s'", con);
    CHECK(run("DIR | FIND /C \"TXT\"") == 0 && strcmp(con, "2\n") == 0, "a pipe: '%s'", con);

    /* /P pauses a full screen; Esc stops it. */
    CHECK(f_mkdir("/MANY") == FR_OK, "many");
    for (int i = 0; i < 40; i++) {
        char name[32];
        snprintf(name, sizeof name, "/MANY/F%02d.TXT", i);
        CHECK(put(name, "x"), "%s", name);
    }
    keys = " ";
    CHECK(run("DIR \\MANY /P") == 0 && has("F26      TXT          1\nPress any key to continue . . .\nF27") &&
              has("bytes free"),
          "pauses: '%s'", con);
    keys = "\x1b";
    CHECK(run("DIR \\MANY /P") == 130 && has("Press any key") && !has("F27") && !has("bytes free"),
          "Esc stops: '%s'", con);
    CHECK(run("DIR \\MANY | FIND /C \"TXT\"") == 0 && strcmp(con, "40\n") == 0,
          "no pause into a pipe: '%s'", con);
    keys = "";

    /* REN. */
    CHECK(run("REN *.BAK *.OLD") == 0 && exists("/HOME/A.OLD") && !exists("/HOME/A.BAK"),
          "ren mask: '%s'", con);
    CHECK(run("REN A.OLD B.OLD") == 1 && strcmp(con, "Duplicate file name or file not found\n") == 0,
          "'%s'", con);
    CHECK(run("REN a.old a.old") == 0 && run("REN a.old a.Old") == 0, "case: '%s'", con);
    CHECK(run("REN A.OLD DOCS\\X") == 1 && strcmp(con, "Invalid parameter\n") == 0, "'%s'", con);
    CHECK(run("REN DOCS PAPERS") == 0 && exists("/HOME/PAPERS"), "a directory");

    /* ATTRIB, and DEL against a read-only file. */
    CHECK(run("ATTRIB +R A.TXT") == 0 && (attrs("/HOME/A.TXT") & AM_RDO), "+R");
    CHECK(run("ATTRIB A.TXT") == 0 && strcmp(con, "A    R     C:\\HOME\\A.TXT\n") == 0, "'%s'",
          con);
    CHECK(run("ATTRIB") == 0 && has("C:\\HOME\\.hidden"), "all, hidden too: '%s'", con);
    CHECK(run("DEL A.TXT") == 1 && strcmp(con, "Access denied - C:\\HOME\\A.TXT\n") == 0 &&
              exists("/HOME/A.TXT"),
          "'%s'", con);
    CHECK(run("ATTRIB -R +H A.TXT") == 0 && attrs("/HOME/A.TXT") == (AM_ARC | AM_HID), "-R +H");
    CHECK(run("ATTRIB +X A.TXT") == 1 && has("Parameter format not correct - +X"), "'%s'", con);
    CHECK(run("ATTRIB -H A.TXT") == 0, "-H");

    /* DEL: *.* asks first. */
    CHECK(run("COPY *.OLD PAPERS") == 0, "more to delete");
    keys = "xn";
    CHECK(run("DEL PAPERS") == 0 && has("All files in directory will be deleted!\nAre you sure (Y/N)?") &&
              exists("/HOME/PAPERS/A.OLD"),
          "no: '%s'", con);
    keys = "y";
    CHECK(run("DEL PAPERS\\*.*") == 0 && !exists("/HOME/PAPERS/A.OLD") &&
              !exists("/HOME/PAPERS/A.BAK"),
          "yes: '%s'", con);
    CHECK(run("COPY *.OLD PAPERS") == 0, "again");
    /* One kept, one deleted, in the card's order. */
    keys = "ny";
    CHECK(run("DEL /P PAPERS\\*.OLD") == 0 && has("Delete (Y/N)?N\n") && has("Delete (Y/N)?Y\n") &&
              exists("/HOME/PAPERS/A.OLD") != exists("/HOME/PAPERS/B.OLD"),
          "/P: '%s'", con);
    keys = "";
    CHECK(run("DEL NOPE.*") == 1 && strcmp(con, "File not found\n") == 0, "'%s'", con);
    CHECK(run("DEL ._A.TXT") == 0 && !exists("/HOME/._A.TXT"), "a hidden file by name");

    /* RD. */
    CHECK(run("RD PAPERS") == 1 && has("Invalid path, not directory,\nor directory not empty"),
          "'%s'", con);
    CHECK(run("MD EMPTY") == 0 && run("RD EMPTY") == 0 && !exists("/HOME/EMPTY"), "rd");
    CHECK(run("RD \\HOME") == 1 && strcmp(con, "Attempt to remove current directory - C:\\HOME\n") == 0,
          "'%s'", con);

    /* XCOPY /S, its hidden files left out, and a cyclic copy refused. */
    CHECK(f_mkdir("/HOME/PAPERS/DEEP") == FR_OK && put("/HOME/PAPERS/DEEP/D.TXT", "deep") &&
              put("/HOME/PAPERS/.secret", "s"),
          "a tree");
    CHECK(run("XCOPY PAPERS \\BACKUP /S") == 0 && has("Reading source file(s)...") &&
              has("DEEP\\D.TXT\n") && has("2 File(s) copied"),
          "xcopy: '%s'", con);
    CHECK(strcmp(slurp("/BACKUP/DEEP/D.TXT"), "deep") == 0 &&
              (exists("/BACKUP/A.OLD") || exists("/BACKUP/B.OLD")) && !exists("/BACKUP/.secret"),
          "the copy");
    CHECK(run("XCOPY \\HOME \\HOME\\X /S") == 4 && has("Cannot perform a cyclic copy"), "'%s'",
          con);
    CHECK(run("XCOPY \\BACKUP\\*.TXT \\FLAT") == 1 && !exists("/FLAT") &&
              has("File not found - \\BACKUP\\*.TXT") && has("0 File(s) copied"),
          "no TXT at the top: '%s'", con);
    CHECK(run("XCOPY \\BACKUP\\DEEP\\D.TXT \\D2.TXT") == 0 && exists("/D2.TXT"), "one file: '%s'",
          con);

    /* MOVE. */
    CHECK(run("MOVE B.TXT PAPERS") == 0 && has("C:\\HOME\\B.TXT => C:\\HOME\\PAPERS\\B.TXT [ok]") &&
              exists("/HOME/PAPERS/B.TXT") && !exists("/HOME/B.TXT"),
          "move: '%s'", con);
    CHECK(run("MOVE \\BACKUP \\SAVED") == 0 && exists("/SAVED/DEEP/D.TXT"), "rename a dir: '%s'",
          con);
    CHECK(run("MOVE *.OLD A.TXT") == 1 && has("Cannot move multiple files to a single file"),
          "'%s'", con);
    CHECK(run("MOVE \\SAVED \\SAVED\\DEEP") == 1 && has("[Cannot move a directory into itself]"),
          "'%s'", con);
    CHECK(put("/HOME/C.TXT", "new") && put("/HOME/PAPERS/C.TXT", "old") &&
              run("MOVE C.TXT,A.OLD PAPERS") == 0 &&
              strcmp(slurp("/HOME/PAPERS/C.TXT"), "new") == 0 && exists("/HOME/PAPERS/A.OLD"),
          "a list, a file replaced: '%s'", con);

    /* DELTREE: hidden and read-only files go too; the current directory
     * cannot. */
    CHECK(run("ATTRIB +R \\SAVED\\DEEP\\D.TXT") == 0, "read-only inside");
    keys = "n";
    CHECK(run("DELTREE \\SAVED") == 0 &&
              has("Delete directory \"SAVED\" and all its subdirectories? [yn] N") &&
              exists("/SAVED"),
          "no: '%s'", con);
    keys = "y";
    CHECK(run("DELTREE \\SAVED") == 0 && has("Deleting SAVED...") && !exists("/SAVED"),
          "yes: '%s'", con);
    keys = "";
    CHECK(run("DELTREE /Y \\D2.TXT") == 0 && !exists("/D2.TXT"), "a file");
    CHECK(run("DELTREE /Y \\HOME") == 1 && has("Attempt to remove current directory"), "'%s'",
          con);

    /* FIND. */
    CHECK(put("/HOME/F.TXT", "Apple\nbanana\nAPPLE pie\ncherry\n"), "f.txt");
    CHECK(run("FIND \"pple\" F.TXT") == 0 && strcmp(con, "\n---------- F.TXT\nApple\n") == 0,
          "'%s'", con);
    CHECK(run("FIND /I /N \"apple\" f.txt") == 0 &&
              strcmp(con, "\n---------- F.TXT\n[1]Apple\n[3]APPLE pie\n") == 0,
          "'%s'", con);
    /* Case counts without /I: only banana has an a. */
    CHECK(run("FIND /V /C \"a\" F.TXT") == 0 && strcmp(con, "---------- F.TXT: 3\n") == 0,
          "'%s'", con);
    CHECK(run("TYPE F.TXT | FIND \"an\"") == 0 && strcmp(con, "banana\n") == 0, "'%s'", con);
    CHECK(run("FIND \"kiwi\" < F.TXT") == 1 && strcmp(con, "") == 0, "none");
    CHECK(run("FIND \"x\" NOPE.TXT") == 2 && has("File not found - NOPE.TXT"), "'%s'", con);
    CHECK(run("FIND") == 2 && has("FIND: Required parameter missing"), "'%s'", con);

    /* MORE copies when its output is not the screen. */
    CHECK(run("MORE F.TXT > G.TXT") == 0 &&
              strcmp(slurp("/HOME/G.TXT"), "Apple\nbanana\nAPPLE pie\ncherry\n") == 0,
          "more");
    CHECK(run("MORE") == 1 && has("Required parameter missing"), "'%s'", con);

    /* SET, %NAME%, PATH and PROMPT. */
    CHECK(run("SET greeting=hi there") == 0 && run("ECHO [%GREETING%] [%greeting%] 100%") == 0 &&
              strcmp(con, "[hi there] [hi there] 100%\n") == 0,
          "'%s'", con);
    CHECK(run("SET") == 0 && has("PATH=C:\\PICO2-APPS\nPROMPT=$P$G\n") &&
              has("GREETING=hi there\n"),
          "'%s'", con);
    CHECK(run("SET GREETING=") == 0 && !vars_get("GREETING", 8), "cleared");
    CHECK(run("SET NOEQUALS") == 1 && strcmp(con, "Syntax error\n") == 0, "'%s'", con);
    CHECK(run("PATH") == 0 && strcmp(con, "PATH=C:\\PICO2-APPS\n") == 0, "'%s'", con);
    CHECK(run("PATH ;") == 0 && run("PATH") == 0 && strcmp(con, "No Path\n") == 0, "'%s'", con);
    CHECK(run("path c:\\pico2-apps;\\HOME") == 0 &&
              strcmp(vars_get("PATH", 4), "C:\\PICO2-APPS;\\HOME") == 0,
          "%s", vars_get("PATH", 4));
    CHECK(run("PROMPT [$p]$_$n$g") == 0, "prompt");
    clear();
    sh.at_bol = true;
    shell_prompt(&sh);
    CHECK(strcmp(con, "[C:\\HOME]\nC>") == 0 && sh.line.prompt == 2, "'%s' %zu", con,
          sh.line.prompt);
    CHECK(run("PROMPT") == 0, "default");
    clear();
    sh.at_bol = true;
    shell_prompt(&sh);
    CHECK(strcmp(con, "C:\\HOME>") == 0 && sh.line.prompt == 8, "'%s'", con);
    CHECK(run("ECHO OFF") == 0, "echo off");
    clear();
    sh.at_bol = true;
    shell_prompt(&sh);
    CHECK(strcmp(con, "") == 0 && sh.line.prompt == 0, "no prompt: '%s'", con);
    CHECK(run("ECHO") == 0 && strcmp(con, "ECHO is off\n") == 0 && run("ECHO ON") == 0, "'%s'",
          con);

    /* Programs: in the current directory first, then PATH. */
    CHECK(put("/HOME/HERE.UF2", "not a uf2") && put("/pico2-apps/THERE.UF2", "nor this"),
          "programs");
    CHECK(run("here") == 1 && has("cannot run programs"), "found here: '%s'", con);
    CHECK(run("THERE") == 1 && has("cannot run programs"), "found on PATH: '%s'", con);
    CHECK(run("\\pico2-apps\\there.uf2") == 1 && has("cannot run programs"), "by path: '%s'",
          con);

    /* VER, VOL, MEM, HELP. */
    CHECK(run("VER") == 0 && strcmp(con, "\nPicoCalc shell, DOS flavour\nVersion test\n\n") == 0, "'%s'",
          con);
    CHECK(run("VOL") == 0 && has(" Volume in drive C is PICOCALC\n Volume Serial Number is "),
          "'%s'", con);
    CHECK(run("VOL D:") == 1 && has("Invalid drive specification"), "'%s'", con);
    CHECK(run("MEM") == 1 && has("MEM: not on this machine"), "'%s'", con);
    shell_set_memory(&sh, 400000, 532480);
    CHECK(run("MEM") == 0 && has("    532480 bytes total memory\n") &&
              has("    132480 bytes free\n"),
          "'%s'", con);
    CHECK(run("HELP") == 0 && has("ATTRIB") && has("XCOPY") && has("HELP name for one"), "'%s'",
          con);
    CHECK(run("help copy") == 0 && strcmp(con, "usage: COPY source [destination]\ncopy files\n") == 0,
          "'%s'", con);
    CHECK(run("PHOSPHOR GREEN") == 1 && has("phosphor: not on this machine"), "lower: '%s'", con);

    /* The prompt's keys: F3 the rest of the last line, Esc drops the line,
     * Tab completes with '\'. */
    sh.status = 0;
    history_add("ECHO from history");
    clear();
    sh.at_bol = true;
    shell_prompt(&sh);
    shell_input(&sh, 'E');
    shell_input(&sh, 'C');
    shell_input(&sh, KEY_F3);
    CHECK(strcmp(sh.line.text, "ECHO from history") == 0, "F3: '%s'", sh.line.text);
    shell_input(&sh, 0x1B);
    CHECK(sh.line.len == 0, "Esc: '%s'", sh.line.text);
    for (const char *s = "CD PAP\t"; *s; s++) shell_input(&sh, (unsigned char)*s);
    CHECK(strcmp(sh.line.text, "CD PAPERS\\") == 0, "Tab: '%s'", sh.line.text);
    for (const char *s = "DE\t"; *s; s++) shell_input(&sh, (unsigned char)*s);
    CHECK(strcmp(sh.line.text, "CD PAPERS\\DEEP\\") == 0, "Tab again: '%s'", sh.line.text);
    shell_input(&sh, 0x1B);
    for (const char *s = "xc\t"; *s; s++) shell_input(&sh, (unsigned char)*s);
    CHECK(strcmp(sh.line.text, "XCOPY ") == 0, "a command: '%s'", sh.line.text);
    shell_input(&sh, 0x1B);

    /* A copy that fails part way leaves the old file. */
    CHECK(put("/HOME/OLD.TXT", "the old text"), "old");
    static char big[20000];
    memset(big, 'x', sizeof big - 1);
    CHECK(put("/HOME/BIG.TXT", big), "big");
    host_disk_fail_writes_after(3);
    CHECK(run("COPY BIG.TXT OLD.TXT") == 1 && has("0 file(s) copied"), "fails: '%s'", con);
    host_disk_fail_writes_after(-1);
    files_mount();   /* as after the card was taken out and put back */
    CHECK(slurp("/HOME/OLD.TXT") && strcmp(slurp("/HOME/OLD.TXT"), "the old text") == 0,
          "old file kept");

    files_unmount();
    host_disk_close();
    return 0;
}

int main(void) {
    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now, interrupted, getkey, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");
    if (unit_tests()) return 1;
    if (dos_tests(false)) return 1;
    shell_init(&sh, &out, &hooks, "test");
    if (dos_tests(true)) return 1;
    TEST_DONE();
}
