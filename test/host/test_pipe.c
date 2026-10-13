/* test_pipe.c — pipes, scripts, /etc/profile and the history file
 * (pipe.h, history.h, design.md §4.3, §4.4), over FAT16 and FAT32. */

#include <stdio.h>
#include <string.h>

#include "files.h"
#include "history.h"
#include "host_disk.h"
#include "shell.h"
#include "test_util.h"

static shell_t sh;
static char    con[1 << 17];
static size_t  con_len;
static int     interrupt_after = -1;

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

/* usbdisk's hook: whether the volume was let go while it ran, and what
 * it says. */
static int         usb_calls;
static FRESULT     usb_seen;
static const char *usb_says;

static const char *fake_usbdisk(void *ctx) {
    (void)ctx;
    FILINFO fno;
    usb_calls++;
    usb_seen = f_stat("/a.txt", &fno);
    return usb_says;
}

static int run(const char *line) {
    con_len = 0;
    con[0] = '\0';
    return shell_run(&sh, line);
}

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

/* `lines` numbered lines, "line 1\n" and so on. */
static const char *numbered(unsigned lines) {
    static char buf[1 << 17];
    size_t n = 0;
    for (unsigned i = 1; i <= lines; i++) n += (size_t)snprintf(buf + n, sizeof buf - n, "line %u\n", i);
    return buf;
}

static int pipe_tests(bool fat32) {
    const char *kind = fat32 ? "FAT32" : "FAT16";
    CHECK(host_disk_create(NULL, fat32 ? 64u << 20 : 32u << 20, fat32), "%s image", kind);
    files_mount();
    shell_start(&sh);

    /* Small: through RAM, no file. */
    CHECK(put("/a.txt", "one\ntwo\nthree\n"), "a.txt");
    CHECK(run("cat a.txt | cat") == 0 && strcmp(con, "one\ntwo\nthree\n") == 0, "%s: |: '%s'",
          kind, con);
    CHECK(!exists("/.shell"), "a small pipe needs no file");
    CHECK(run("cat a.txt | grep t | grep -v h") == 0 && strcmp(con, "two\n") == 0, "a|b|c: '%s'",
          con);
    CHECK(run("echo hi | cat > b.txt") == 0 && con_len == 0 && strcmp(slurp("/b.txt"), "hi\n") == 0,
          "| then >: '%s'", con);
    CHECK(run("cat < a.txt | grep one") == 0 && strcmp(con, "one\n") == 0, "< then |: '%s'", con);
    /* ls into a pipe is one name to a line, as into a file. */
    CHECK(run("ls / | cat") == 0 && strcmp(con, "a.txt\nb.txt\n") == 0, "ls |: '%s'", con);
    /* Errors go to the terminal, not down the pipe. */
    CHECK(run("cat nope | cat") == 0 && strcmp(con, "cat: nope: no such file or directory\n") == 0,
          "error: '%s'", con);
    /* The status is the last command's. */
    CHECK(run("cat nope | cat; echo $?") == 0 && strstr(con, "\n0\n"), "status: '%s'", con);
    CHECK(run("cat a.txt | grep nothing; echo $?") == 0 && strcmp(con, "1\n") == 0,
          "status 1: '%s'", con);
    /* && and || take the whole pipeline. */
    CHECK(run("cat a.txt | grep -c o && echo yes || echo no") == 0 && strcmp(con, "2\nyes\n") == 0,
          "&&: '%s'", con);
    CHECK(run("false_cmd | cat || echo no") == 0 && strstr(con, "not found"), "||: '%s'", con);

    /* Large: past SHELL_PIPE_BUF it goes through /.shell, which goes. */
    const char *big = numbered(4000);   /* about 40 KB */
    CHECK(strlen(big) > SHELL_PIPE_BUF, "big enough");
    CHECK(put("/big.txt", big), "big.txt");
    CHECK(run("cat big.txt | cat > big2.txt") == 0, "big |: '%s'", con);
    const char *s = slurp("/big2.txt");
    CHECK(s && strcmp(s, big) == 0, "%s: the big pipe is whole", kind);
    CHECK(run("ls -a /.shell") == 0 && con_len == 0, "the pipe's file is gone: '%s'", con);
    /* Three stages, both large: the two RAM buffers, then a file. */
    CHECK(run("cat big.txt | cat | cat | grep -c line") == 0 && strcmp(con, "4000\n") == 0,
          "big a|b|c|d: '%s'", con);
    CHECK(run("ls -a /.shell") == 0 && con_len == 0, "no pipe files left: '%s'", con);

    /* A card that fails as the pipe spills: the loss is said, the next
     * stage does not run on part of the output, and the shell carries on
     * once the card is back. */
    host_disk_fail_writes_after(0);
    static const char lost[] = "shell: pipe: card I/O error\n";
    CHECK(run("cat big.txt | grep -c line") == 1 && con_len >= sizeof lost - 1 &&
              strcmp(con + con_len - (sizeof lost - 1), lost) == 0,
          "a pipe that cannot spill: '%s'", con);
    host_disk_fail_writes_after(-1);
    files_mount();
    CHECK(run("cat big.txt | grep -c line") == 0 && strcmp(con, "4000\n") == 0, "after: '%s'",
          con);
    CHECK(run("ls -a /.shell") == 0 && con_len == 0, "no pipe file left: '%s'", con);

    /* Ctrl-C in the first stage stops the line. */
    interrupt_after = 1;
    CHECK(run("cat big.txt | grep -c line; echo after") == 130 && !strstr(con, "after"),
          "stopped: '%s'", con);
    interrupt_after = -1;

    /* A program in a pipe is refused, before anything is flashed. */
    CHECK(run("echo x | menu") == 1 && strstr(con, "not with a redirection or a pipe"),
          "menu in a pipe: '%s'", con);

    /* ---- scripts ---- */
    CHECK(put("/s1", "# a script\necho one\nA=2\necho $A > /s1.out\n\ncd /\n"), "s1");
    CHECK(run("sh /s1; pwd; cat /s1.out") == 0 && strcmp(con, "one\n/\n2\n") == 0,
          "sh: '%s'", con);
    /* CR LF scripts, and a last line with no line break. */
    CHECK(put("/s2", "echo a\r\necho b"), "s2");
    CHECK(run("sh /s2") == 0 && strcmp(con, "a\nb\n") == 0, "CR LF: '%s'", con);
    /* A mistake names the file and line, and the script goes on. */
    CHECK(put("/s3", "echo a\necho 'b\necho c\n"), "s3");
    CHECK(run("sh /s3") == 0 && strcmp(con, "a\nsh: /s3: line 2: unterminated '\nc\n") == 0,
          "line: '%s'", con);
    /* A line too long is refused. */
    char longer[SHELL_LINE_MAX + 40];
    memset(longer, 'x', sizeof longer);
    memcpy(longer, "echo ", 5);
    longer[sizeof longer - 2] = '\n';
    longer[sizeof longer - 1] = '\0';
    char script[sizeof longer + 32];
    snprintf(script, sizeof script, "%secho after\n", longer);
    CHECK(put("/s4", script), "s4");
    CHECK(run("sh /s4") == 0 && strstr(con, "sh: /s4: line 1: line too long\nafter\n"), "long: '%s'",
          con);
    /* Output and redirection pass into the script; pipes inside it work. */
    CHECK(put("/s5", "echo x\ncat /a.txt | grep o\n"), "s5");
    CHECK(run("sh /s5 > /s5.out") == 0 && con_len == 0 &&
              strcmp(slurp("/s5.out"), "x\none\ntwo\n") == 0,
          "sh > f: '%s' '%s'", con, slurp("/s5.out"));
    CHECK(run("sh /s5 | grep -c .") == 0 && strcmp(con, "3\n") == 0, "sh | grep: '%s'", con);
    /* Nesting: within limits, and past them. */
    CHECK(put("/n1", "echo n1\nsh /n2\n") && put("/n2", "echo n2\nsh /n3\n") &&
              put("/n3", "echo n3\nsh /n1\n"),
          "n1-n3");
    CHECK(run("sh /n1") != 0 && strstr(con, "n1\nn2\nn3\nsh: /n1: scripts nested more than 3 deep"),
          "nesting: '%s'", con);
    CHECK(run("echo still ok") == 0 && strcmp(con, "still ok\n") == 0 && sh.depth == 0,
          "back at level 0");
    CHECK(run("sh") == 1 && strstr(con, "usage: sh file"), "sh alone");
    CHECK(run("sh /nope") == 1 && strstr(con, "sh: /nope: no such file"), "sh nope: '%s'", con);
    /* A stop ends the script and the line. */
    CHECK(put("/s6", "cat /big.txt\necho never\n"), "s6");
    interrupt_after = 1;
    CHECK(run("sh /s6; echo never") == 130 && !strstr(con, "never"), "stop: '%s'", con);
    interrupt_after = -1;

    /* ---- /etc/profile ---- */
    CHECK(f_mkdir("/etc") == FR_OK && f_mkdir("/home") == FR_OK && f_mkdir("/w") == FR_OK, "dirs");
    CHECK(put("/etc/profile", "GREETING=hello\ncd /w\necho profile ran\n"), "profile");
    con_len = 0;
    shell_start(&sh);
    CHECK(strstr(con, "profile ran\n/w>") && strcmp(sh.cwd, "/w") == 0, "profile: '%s'", con);
    CHECK(run("echo $GREETING") == 0 && strcmp(con, "hello\n") == 0, "its variable: '%s'", con);
    /* Lost between unlink and rename: the whole .new is read. */
    CHECK(f_rename("/etc/profile", "/etc/profile.new") == FR_OK, "only .new");
    con_len = 0;
    shell_start(&sh);
    CHECK(strstr(con, "profile ran\n"), "profile.new: '%s'", con);
    CHECK(f_unlink("/etc/profile.new") == FR_OK, "no profile");
    con_len = 0;
    shell_start(&sh);
    CHECK(strcmp(sh.cwd, "/home") == 0 && strstr(con, "/home>"), "no profile: '%s'", con);

    /* ---- the history file ---- */
    history_clear();
    shell_start(&sh);
    const char *typed[] = { "echo one\r", "   \r", "echo two\r", "echo two\r", "ls\r" };
    for (size_t i = 0; i < sizeof typed / sizeof typed[0]; i++)
        for (const char *c = typed[i]; *c; c++) shell_input(&sh, (unsigned char)*c);
    CHECK(history_count() == 3, "blank and repeated lines are not kept: %u", history_count());
    CHECK(!exists(HISTORY_FILE), "not written until the shell is left");
    shell_leaving(&sh);
    s = slurp(HISTORY_FILE);
    CHECK(s && strcmp(s, "echo one\necho two\nls\n") == 0, "%s: history file: '%s'", kind,
          s ? s : "(none)");
    CHECK(!exists(HISTORY_FILE ".new"), "no .new left");
    unsigned long writes = host_disk_writes();
    shell_leaving(&sh);
    CHECK(host_disk_writes() == writes, "unchanged: not written again");
    history_clear();
    shell_start(&sh);
    CHECK(history_count() == 3 && strcmp(history_get(2), "ls") == 0, "read back: %u",
          history_count());
    CHECK(run("history") == 0 && strcmp(con, "   1  echo one\n   2  echo two\n   3  ls\n") == 0,
          "history: '%s'", con);
    /* A failed write keeps the old file, and says so. */
    history_add("echo three");
    host_disk_fail_writes_after(1);
    con_len = 0;
    shell_leaving(&sh);
    host_disk_fail_writes_after(-1);
    CHECK(strstr(con, "shell: /.shell/history: "), "failure said: '%s'", con);
    s = slurp(HISTORY_FILE);
    CHECK(s && strcmp(s, "echo one\necho two\nls\n") == 0, "old history kept: '%s'", s ? s : "");
    shell_leaving(&sh);
    s = slurp(HISTORY_FILE);
    CHECK(s && strstr(s, "echo three\n"), "written next time: '%s'", s ? s : "");
    /* The oldest go to make room. */
    for (unsigned i = 0; i < 400; i++) {
        char line[32];
        snprintf(line, sizeof line, "echo %u", i);
        history_add(line);
    }
    CHECK(history_count() < 400 && strcmp(history_get(history_count() - 1), "echo 399") == 0,
          "full: %u kept", history_count());

    /* ---- usbdisk ---- */
    CHECK(run("usbdisk") == 1 && strcmp(con, "usbdisk: no USB here\n") == 0, "no USB: '%s'", con);
    sh.hooks.usbdisk = fake_usbdisk;
    usb_calls = 0;
    usb_says = NULL;
    history_add("echo before usb");
    CHECK(run("usbdisk") == 0 && usb_calls == 1 && strstr(con, "The card is on USB"),
          "usbdisk: '%s'", con);
    CHECK(usb_seen == FR_NOT_ENABLED, "FatFs let the card go: %d", (int)usb_seen);
    s = slurp(HISTORY_FILE);
    CHECK(s && strstr(s, "echo before usb\n"), "history saved first: '%s'", s ? s : "");
    CHECK(run("cat /a.txt") == 0 && strcmp(con, "one\ntwo\nthree\n") == 0, "mounted after: '%s'",
          con);
    usb_says = "the card was taken out";
    CHECK(run("usbdisk") == 1 && strstr(con, "usbdisk: the card was taken out"), "error: '%s'", con);
    usb_calls = 0;
    CHECK(run("usbdisk > /u.txt") == 1 && usb_calls == 0 && !exists("/u.txt.new"),
          "> refused: '%s'", con);
    CHECK(run("echo | usbdisk") == 1 && usb_calls == 0, "| refused: '%s'", con);
    CHECK(run("usbdisk x") == 1 && usb_calls == 0 && strstr(con, "usage: usbdisk"), "usage");
    sh.hooks.usbdisk = NULL;

    host_disk_close();
    return test_failures;
}

int main(void) {
    const out_t out = { sink, NULL, false };
    const shell_hooks_t hooks = { now, interrupted, NULL, NULL, NULL };
    shell_init(&sh, &out, &hooks, "test");
    if (pipe_tests(false)) return 1;
    shell_init(&sh, &out, &hooks, "test");
    if (pipe_tests(true)) return 1;
    TEST_DONE();
}
