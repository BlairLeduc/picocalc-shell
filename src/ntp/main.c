/* main.c — ntp.uf2: set the shell's clock from NTP over Wi-Fi, then go
 * back to the shell (design.md §3.8).
 *
 * A RAM program for the Pico 2 W, run by the W build's `ntp` through
 * stage3's BOOT_RAM, so the program in flash is untouched. It reads
 * /ntp/ntp.cfg, joins the one network it names, asks the server once
 * (SNTP, RFC 4330, over UDP), sets the AON timer and the clock's record
 * (src/port/clock.c), and keeps what happened in the record, where the
 * shell's `ntp` reads it. Then it sends BOOT_SD (src/port/loader.c).
 * The DS3231, if any, is written by the shell when it starts: this has
 * no I2C driver of its own for it.
 *
 * Everything is bounded by the file's timeout, and Esc, Break or Ctrl-C
 * (or, in the development build, Esc or Ctrl-C on UART0) stops it. It
 * shows what it does on the panel with the shell's own terminal. */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "aon.h"
#include "clock.h"
#include "ff.h"
#include "kbd.h"
#include "keycodes.h"
#include "keys.h"
#include "lcd.h"
#include "loader.h"
#include "ntp.h"
#include "screen.h"
#include "sd.h"
#include "southbridge.h"
#include "term.h"

#define KBD_POLL_US 33333u     /* about 30 Hz (hardware-notes.md §6.1) */
#define RESEND_US   2000000u   /* a request with no answer is sent again */
#define NTP_PORT    123u
#define NTP_UNIX    2208988800u   /* 1900 to 1970, in seconds */
#define SHOW_MS     1500u         /* time to read the result before going back */

static term_t  s_term;
static keys_t  s_keys;
static FATFS   s_fs;
static FIL     s_file;
static char    s_text[NTP_CFG_BYTES];
static ntp_config_t s_cfg;
static uint64_t s_next_poll;

/* To the panel and, in the development build, UART0. */
static void say(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
#if PICO_SHELL_UART
    fputs(buf, stdout);
#endif
    /* The terminal's LF only moves down. */
    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n') term_write(&s_term, "\r", 1);
        term_write(&s_term, &buf[i], 1);
    }
    screen_flush(&s_term, time_us_64());
}

/* Whether the user stopped it: polled at the keyboard's rate. */
static bool stopped(void) {
    uint64_t now = time_us_64();
    screen_tick(&s_term, now);
    if (now < s_next_poll) return false;
    s_next_poll = now + KBD_POLL_US;
    bool hit = false;
    kbd_poll();
    uint8_t state, code;
    while (kbd_pop(&state, &code)) {
        int k = keys_event(&s_keys, state, code);
        if (k == KEY_BREAK || k == 0x1B || k == 0x03) hit = true;
    }
#if PICO_SHELL_UART
    int c;
    while ((c = getchar_timeout_us(0)) >= 0)
        if (c == 0x1B || c == 0x03) hit = true;
#endif
    return hit;
}

/* ---- the settings ------------------------------------------------------ */

static ntp_result_t read_config(void) {
    say("Reading %s\n", NTP_CFG_PATH);
    if (!sd_present() || f_mount(&s_fs, "", 1) != FR_OK) {
        say("  The card cannot be read.\n");
        return NTP_NO_CONFIG;
    }
    FRESULT fr = f_open(&s_file, NTP_CFG_PATH, FA_READ);
    if (fr != FR_OK) {
        say("  %s\n", fr == FR_NO_FILE || fr == FR_NO_PATH ? "It is not there." : "It cannot be read.");
        f_unmount("");
        return NTP_NO_CONFIG;
    }
    UINT n = 0;
    fr = f_read(&s_file, s_text, sizeof s_text, &n);
    bool whole = f_size(&s_file) <= sizeof s_text;
    f_close(&s_file);
    f_unmount("");
    if (fr != FR_OK) {
        say("  It cannot be read.\n");
        return NTP_NO_CONFIG;
    }
    if (!whole) {
        say("  It is over %u bytes.\n", (unsigned)sizeof s_text);
        return NTP_BAD_CONFIG;
    }
    unsigned line;
    ntp_cfg_status_t st = ntp_config_parse(&s_cfg, s_text, n, &line);
    if (st != NTP_CFG_OK) {
        if (line) say("  Line %u: %s.\n", line, ntp_cfg_status_str(st));
        else say("  It has %s.\n", ntp_cfg_status_str(st));
        return NTP_BAD_CONFIG;
    }
    return NTP_SET;
}

/* ---- the network ------------------------------------------------------- */

static ntp_result_t join(uint64_t deadline) {
    say("Joining %s\n", s_cfg.ssid);
    cyw43_arch_enable_sta_mode();
    uint32_t auth = s_cfg.password[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
    const char *pw = s_cfg.password[0] ? s_cfg.password : NULL;
    if (cyw43_arch_wifi_connect_async(s_cfg.ssid, pw, auth)) return NTP_NO_NETWORK;
    int link = CYW43_LINK_DOWN;
    while (time_us_64() < deadline) {
        cyw43_arch_poll();
        link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        if (link == CYW43_LINK_UP) {
            say("  Joined, as %s\n", ip4addr_ntoa(netif_ip4_addr(netif_default)));
            return NTP_SET;
        }
        if (link == CYW43_LINK_BADAUTH) return NTP_BAD_PASSWORD;
        if (link == CYW43_LINK_FAIL) return NTP_NO_NETWORK;
        /* A scan that missed the network: ask again, as the SDK's own
         * cyw43_arch_wifi_connect_timeout_ms() does, until the timeout.
         * The first answer on the Plus 2 W was this, in 1.4 s, with the
         * network in range (2026-10-22). */
        if (link == CYW43_LINK_NONET && cyw43_arch_wifi_connect_async(s_cfg.ssid, pw, auth))
            return NTP_NO_NETWORK;
        if (stopped()) return NTP_STOPPED;
    }
    /* Joined, but DHCP gave no address in time. */
    return link == CYW43_LINK_JOIN || link == CYW43_LINK_NOIP ? NTP_NO_ADDRESS : NTP_NO_NETWORK;
}

typedef struct {
    bool      done;
    bool      found;
    ip_addr_t addr;
} lookup_t;

static void found(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name;
    lookup_t *l = arg;
    l->done = true;
    if (addr) {
        l->found = true;
        l->addr = *addr;
    }
}

static ntp_result_t lookup(ip_addr_t *addr, uint64_t deadline) {
    lookup_t l = { 0 };
    err_t err = dns_gethostbyname(s_cfg.server, &l.addr, found, &l);
    if (err == ERR_OK) l.done = l.found = true;
    else if (err != ERR_INPROGRESS) return NTP_NO_SERVER;
    while (!l.done && time_us_64() < deadline) {
        cyw43_arch_poll();
        if (stopped()) return NTP_STOPPED;
    }
    if (!l.found) return NTP_NO_SERVER;
    *addr = l.addr;
    return NTP_SET;
}

/* ---- SNTP --------------------------------------------------------------- */

typedef struct {
    uint8_t  nonce[8];   /* the request's transmit time, which the reply echoes */
    uint64_t sent_us;    /* time_us_64() as the request went */
    bool     answered;
    int64_t  utc_us;     /* UTC when the reply came, ... */
    uint64_t at_us;      /* ... which was this time_us_64() */
    int64_t  delay_us;   /* the round trip, less the server's time */
} sntp_t;

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* An NTP timestamp, in microseconds since 1970. Era 0 ends in 2036; a
 * seconds field with its top bit clear is taken as era 1. */
static int64_t ntp_us(const uint8_t *p) {
    uint64_t s = get32(p);
    if (!(s & 0x80000000u)) s += 1ull << 32;
    uint64_t frac_us = ((uint64_t)get32(p + 4) * 1000000u) >> 32;
    return (int64_t)((s - NTP_UNIX) * 1000000u + frac_us);
}

static void reply(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)pcb;
    (void)addr;
    (void)port;
    uint64_t now = time_us_64();
    sntp_t *s = arg;
    uint8_t b[48];
    if (!s->answered && p->tot_len >= sizeof b && pbuf_copy_partial(p, b, sizeof b, 0) == sizeof b) {
        /* A server's reply (mode 4), synchronised (leap indicator not 3),
         * not a kiss-o'-death (stratum 0), to this request. */
        bool ok = (b[0] & 7) == 4 && b[0] >> 6 != 3 && b[1] >= 1 && b[1] <= 15 &&
                  memcmp(b + 24, s->nonce, 8) == 0 && get32(b + 40) != 0;
        if (ok) {
            int64_t t2 = ntp_us(b + 32), t3 = ntp_us(b + 40);
            s->delay_us = (int64_t)(now - s->sent_us) - (t3 - t2);
            if (s->delay_us < 0) s->delay_us = 0;
            s->utc_us = t3 + s->delay_us / 2;
            s->at_us = now;
            s->answered = true;
        }
    }
    pbuf_free(p);
}

static void request(struct udp_pcb *pcb, const ip_addr_t *server, sntp_t *s) {
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, 48, PBUF_RAM);
    if (!p) return;
    uint8_t *b = p->payload;
    memset(b, 0, 48);
    b[0] = 0x23;   /* no leap warning, version 4, client */
    s->sent_us = time_us_64();
    for (int i = 0; i < 8; i++) s->nonce[i] = (uint8_t)(s->sent_us >> (8 * i)) ^ (uint8_t)get_rand_32();
    memcpy(b + 40, s->nonce, 8);
    udp_sendto(pcb, p, server, NTP_PORT);
    pbuf_free(p);
}

static ntp_result_t ask(const ip_addr_t *server, sntp_t *s, uint64_t deadline) {
    say("Asking %s\n", ipaddr_ntoa(server));
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) return NTP_NO_REPLY;
    udp_recv(pcb, reply, s);
    uint64_t next = 0;
    ntp_result_t r = NTP_NO_REPLY;
    while (time_us_64() < deadline) {
        if (time_us_64() >= next) {
            request(pcb, server, s);
            next = time_us_64() + RESEND_US;
        }
        cyw43_arch_poll();
        if (s->answered) {
            r = NTP_SET;
            break;
        }
        if (stopped()) {
            r = NTP_STOPPED;
            break;
        }
    }
    udp_remove(pcb);
    return r;
}

/* ---- the program -------------------------------------------------------- */

static ntp_result_t run(void) {
    ntp_result_t r = read_config();
    if (r != NTP_SET) return r;
    uint64_t deadline = time_us_64() + (uint64_t)s_cfg.timeout_s * 1000000u;

    uint32_t country = s_cfg.country[0] ? CYW43_COUNTRY(s_cfg.country[0], s_cfg.country[1], 0)
                                        : CYW43_COUNTRY_WORLDWIDE;
    if (cyw43_arch_init_with_country(country)) return NTP_NO_RADIO;
    ip_addr_t server;
    sntp_t s = { 0 };
    r = join(deadline);
    if (r == NTP_SET) {
        say("Looking up %s\n", s_cfg.server);
        r = lookup(&server, deadline);
    }
    if (r == NTP_SET) r = ask(&server, &s, deadline);
    if (r == NTP_SET) {
        /* The time now: the reply's, and what has passed since. */
        int64_t utc_us = s.utc_us + (int64_t)(time_us_64() - s.at_us);
        aon_set((utc_us + 500) / 1000, AON_SRC_NTP);
        clock_tm_t tm;
        clock_gmt(utc_us / 1000000, &tm);
        say("  %s %s %2d %02d:%02d:%02d UTC %d\n  to within %lu ms\n", clock_wday_name(tm.wday),
            clock_mon_name(tm.mon), tm.mday, tm.hour, tm.min, tm.sec, tm.year,
            (unsigned long)((s.delay_us / 2 + 999) / 1000));
    }
    cyw43_arch_deinit();
    return r;
}

int main(void) {
    stdio_init_all();
#if PICO_SHELL_UART
    gpio_pull_up(PICO_DEFAULT_UART_RX_PIN);
#endif
    sb_init();
    lcd_init();
    screen_init();
    term_init(&s_term);
    keys_init(&s_keys);
    say("\033[1mntp\033[0m: setting the clock (Esc stops)\n\n");

    uint64_t t0 = time_us_64();
    ntp_result_t r = run();
    aon_ntp_keep(r);
    say("\n%s%s, in %lu ms.\n", r == NTP_SET ? "NTP " : "", ntp_result_str(r),
        (unsigned long)((time_us_64() - t0) / 1000));

    uint64_t until = time_us_64() + (r == NTP_SET ? SHOW_MS : 2 * SHOW_MS) * 1000u;
    while (time_us_64() < until) stopped();
    /* Without a card, stage3 would drop to BOOTSEL (design.md §3.1). */
    if (!sd_present()) {
        say("Put the card back to return to\nthe shell.\n");
        while (!sd_present()) stopped();
        sleep_ms(500);
    }
    say("Back to the shell.\n");
    loader_reboot_shell();
}
