/* ntp.h — what ntp.uf2 and the shell's `ntp` share (design.md §3.8): the
 * results ntp.uf2 leaves in the clock's record, in words, and its
 * settings file, /ntp/ntp.cfg.
 *
 * Portable, with no shell in it, so ntp.uf2 links it alone. The builtin
 * itself is in system.c. */
#ifndef SHELL_NTP_H
#define SHELL_NTP_H

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

/* The W build's (design.md §3.8): 1 there and on the host, 0 in the
 * Pico 2 build, which has no `ntp`. */
#ifndef SHELL_NTP
#define SHELL_NTP 0
#endif

#define NTP_CFG_PATH "/ntp/ntp.cfg"

/* What NTP did since power-on, kept in the record (four bits). */
typedef enum {
    NTP_SET,            /* it set the clock */
    NTP_UNFINISHED,     /* ntp.uf2 was started, and did not say */
    NTP_STOPPED,        /* Esc, Break or Ctrl-C */
    NTP_NO_CONFIG,      /* no /ntp/ntp.cfg */
    NTP_BAD_CONFIG,     /* a mistake in it */
    NTP_NO_RADIO,       /* the Wi-Fi chip did not start: a Pico 2 */
    NTP_NO_NETWORK,     /* the network was not found, or would not join */
    NTP_BAD_PASSWORD,
    NTP_NO_ADDRESS,     /* joined, but DHCP gave no address */
    NTP_NO_SERVER,      /* the server's name was not found */
    NTP_NO_REPLY,       /* no time from the server */
    NTP_RESULTS
} ntp_result_t;

/* "set the clock", "no reply from the server", ... */
const char *ntp_result_str(ntp_result_t r);

/* /ntp/ntp.cfg (design.md §3.8): `key = value` lines, after the
 * emulators' settings files (EL §8.7). A value may be in double quotes,
 * and must be to hold a `#` after a space, or a space at either end. */
typedef struct {
    char     server[NTP_SERVER_MAX + 1];
    char     country[3];        /* two letters, upper case; "" worldwide */
    unsigned timeout_s;         /* to join and get the time */
    char     ssid[NTP_SSID_MAX + 1];
    char     password[NTP_PASSWORD_MAX + 1];   /* "": an open network */
} ntp_config_t;

typedef enum {
    NTP_CFG_OK,
    NTP_CFG_SYNTAX,       /* not key = value, or a quote not closed */
    NTP_CFG_UNKNOWN,      /* no such key */
    NTP_CFG_BAD_VALUE,
    NTP_CFG_DUPLICATE,    /* a key given twice */
    NTP_CFG_TOO_LONG,     /* a line or a value */
    NTP_CFG_NO_SSID,      /* no network named */
} ntp_cfg_status_t;

/* The file's text over the defaults. A wrong line changes nothing and
 * the lines after it still apply; the first problem is returned, with
 * its line in *line (0 for the file as a whole: no ssid). */
ntp_cfg_status_t ntp_config_parse(ntp_config_t *cfg, const char *text, size_t len,
                                  unsigned *line);
const char *ntp_cfg_status_str(ntp_cfg_status_t st);

#endif /* SHELL_NTP_H */
