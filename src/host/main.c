/* main.c — the host shell: the real core in a macOS terminal, over a FAT
 * image file (design.md §6).
 *
 *   build/host/shell card.img        # a new 64 MB FAT32 image if none
 *   build/host/shell -16 card.img    # a new one as 32 MB FAT16
 *
 * The terminal is put in raw mode, so keys arrive as the PicoCalc's
 * would: Delete is Backspace and Return is CR. Ctrl-D on an empty line
 * quits. With stdin not a terminal, each line read is typed, and the
 * shell ends at the end of input.
 */

/* clock_gettime and the POSIX terminal calls: strict C11 hides them on
 * glibc, though not on macOS. */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "host_disk.h"
#include "shell.h"

static struct termios s_saved;
static bool s_raw;

static void con_write(void *ctx, const char *buf, size_t n) {
    (void)ctx;
    fwrite(buf, 1, n, stdout);
    fflush(stdout);
}

static uint64_t now_us(void *ctx) {
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

/* Ctrl-C or Esc waiting on the terminal; other keys meanwhile are lost,
 * as on the board. */
static bool interrupted(void *ctx) {
    (void)ctx;
    if (!s_raw) return false;
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    bool hit = false;
    unsigned char c;
    while (read(STDIN_FILENO, &c, 1) == 1)
        if (c == 0x03 || c == 0x1B) hit = true;
    fcntl(STDIN_FILENO, F_SETFL, flags);
    return hit;
}

/* The next key, as the PicoCalc would type it: Delete is Backspace and
 * Return is CR. -1 at the end of input. */
static int getkey(void *ctx) {
    (void)ctx;
    int c = getchar();
    if (c == EOF) return -1;
    if (c == 0x7F) return 0x08;
    if (c == '\n') return '\r';
    return c;
}

/* The next key if one is waiting on the terminal, otherwise -1 at once.
 * Typed from a file or a pipe, the next key, as stdio has it buffered. */
static int pollkey(void *ctx) {
    if (!s_raw) return getkey(ctx);
    int flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    fcntl(STDIN_FILENO, F_SETFL, flags);
    if (n != 1) return -1;
    if (c == 0x7F) return 0x08;
    if (c == '\n') return '\r';
    return c;
}

/* The host's clock: the computer's, moved by whatever `date` sets. */
static int64_t s_clock_skew_ms;

static int64_t host_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static bool clock_get(void *ctx, int64_t *utc_ms) {
    (void)ctx;
    *utc_ms = host_ms() + s_clock_skew_ms;
    return true;
}

static const char *clock_set(void *ctx, int64_t utc_ms) {
    (void)ctx;
    s_clock_skew_ms = utc_ms - host_ms();
    return NULL;
}

static void restore(void) {
    if (s_raw) tcsetattr(STDIN_FILENO, TCSAFLUSH, &s_saved);
    s_raw = false;
}

int main(int argc, char **argv) {
    bool fat16 = argc == 3 && strcmp(argv[1], "-16") == 0;
    if (argc != 2 + fat16) {
        fprintf(stderr, "usage: shell [-16] card.img\n");
        return 2;
    }
    const char *path = argv[argc - 1];
    if (access(path, F_OK) == 0) {
        if (!host_disk_open(path)) {
            fprintf(stderr, "shell: %s: %s\n", path, strerror(errno));
            return 1;
        }
    } else if (!host_disk_create(path, fat16 ? 32u << 20 : 64u << 20, !fat16)) {
        fprintf(stderr, "shell: %s: cannot make an image\n", path);
        return 1;
    }

    if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &s_saved) == 0) {
        struct termios raw = s_saved;
        raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= (tcflag_t) ~(ICRNL | IXON);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        s_raw = tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0;
    }

    static shell_t sh;
    const out_t out = { con_write, NULL, false };
    const shell_hooks_t hooks = { now_us, interrupted, getkey, pollkey, NULL, NULL };
    shell_init(&sh, &out, &hooks, "host");
    const shell_clock_t clock = { clock_get, clock_set, NULL };
    shell_set_clock(&sh, &clock);
    shell_start(&sh);

    int c;
    while ((c = getkey(NULL)) >= 0) {
        if (c == 0x04 && sh.line.len == 0) break;     /* Ctrl-D */
        shell_input(&sh, c);
    }
    restore();
    fputs("\n", stdout);
    host_disk_close();
    return 0;
}
