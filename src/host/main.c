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
    const shell_hooks_t hooks = { now_us, interrupted, NULL };
    shell_init(&sh, &out, &hooks, "host");
    shell_start(&sh);

    int c;
    while ((c = getchar()) != EOF) {
        if (c == 0x04 && sh.len == 0) break;     /* Ctrl-D */
        if (c == 0x7F) c = 0x08;
        if (c == '\n') c = '\r';
        shell_input(&sh, c);
    }
    restore();
    fputs("\n", stdout);
    host_disk_close();
    return 0;
}
