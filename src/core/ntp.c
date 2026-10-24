/* ntp.c — NTP's results and settings file (ntp.h).
 *
 * The parser is pico-oric's src/core/settings.c (GPL-3.0, the owner's)
 * cut down to five keys, with quoted values. */

#include "ntp.h"

#include <ctype.h>
#include <string.h>

const char *ntp_result_str(ntp_result_t r) {
    switch (r) {
    case NTP_SET:          return "set the clock";
    case NTP_UNFINISHED:   return "ntp.uf2 did not finish";
    case NTP_STOPPED:      return "stopped";
    case NTP_NO_CONFIG:    return "no " NTP_CFG_PATH;
    case NTP_BAD_CONFIG:   return "a mistake in " NTP_CFG_PATH;
    case NTP_NO_RADIO:     return "no Wi-Fi on this board";
    case NTP_NO_NETWORK:   return "the network was not joined";
    case NTP_BAD_PASSWORD: return "the network refused the password";
    case NTP_NO_ADDRESS:   return "the network gave no address";
    case NTP_NO_SERVER:    return "the server was not found";
    case NTP_NO_REPLY:     return "no time from the server";
    default:               return "?";
    }
}

/* ---- /ntp/ntp.cfg -------------------------------------------------------- */

enum { K_SERVER, K_COUNTRY, K_TIMEOUT, K_SSID, K_PASSWORD, K_COUNT };
static const char *const k_names[K_COUNT] = { "server", "country", "timeout", "ssid", "password" };

/* Every default (EL §8.7). */
static void defaults(ntp_config_t *cfg) {
    memset(cfg, 0, sizeof *cfg);
    strcpy(cfg->server, "pool.ntp.org");
    cfg->timeout_s = 20;
}

/* strcasecmp is POSIX, not C11. */
static bool same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
    return *a == *b;
}

static char *trim(char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* A `#` at the start or after a space starts a comment. */
static void strip_comment(char *s) {
    for (char *p = s; *p; p++)
        if (*p == '#' && (p == s || isspace((unsigned char)p[-1]))) {
            *p = 0;
            return;
        }
}

static ntp_cfg_status_t text(char *out, size_t max, const char *v, bool empty_ok) {
    size_t n = strlen(v);
    if (n > max) return NTP_CFG_TOO_LONG;
    if (n == 0 && !empty_ok) return NTP_CFG_BAD_VALUE;
    memcpy(out, v, n + 1);
    return NTP_CFG_OK;
}

/* WPA2's passphrase: 8-63 printable characters, or the key as 64 hex
 * digits; or none, for an open network. */
static ntp_cfg_status_t password(ntp_config_t *cfg, const char *v) {
    size_t n = strlen(v);
    bool ok = n == 0 || (n >= 8 && n <= 63);
    if (n == 64) {
        ok = true;
        for (size_t i = 0; i < n; i++) ok &= isxdigit((unsigned char)v[i]) != 0;
    }
    for (size_t i = 0; ok && i < n; i++) ok = v[i] >= 0x20 && v[i] < 0x7F;
    return ok ? text(cfg->password, NTP_PASSWORD_MAX, v, true) : NTP_CFG_BAD_VALUE;
}

static ntp_cfg_status_t apply(ntp_config_t *cfg, unsigned k, const char *v) {
    switch (k) {
    case K_SERVER:
        /* A name or an address: no spaces. */
        for (const char *p = v; *p; p++)
            if (isspace((unsigned char)*p)) return NTP_CFG_BAD_VALUE;
        return text(cfg->server, NTP_SERVER_MAX, v, false);
    case K_COUNTRY:
        if (strlen(v) != 2 || !isalpha((unsigned char)v[0]) || !isalpha((unsigned char)v[1]))
            return NTP_CFG_BAD_VALUE;
        cfg->country[0] = (char)toupper((unsigned char)v[0]);
        cfg->country[1] = (char)toupper((unsigned char)v[1]);
        cfg->country[2] = 0;
        return NTP_CFG_OK;
    case K_TIMEOUT: {
        unsigned n = 0;
        if (!*v) return NTP_CFG_BAD_VALUE;
        for (; *v; v++) {
            if (!isdigit((unsigned char)*v) || n > 60) return NTP_CFG_BAD_VALUE;
            n = n * 10u + (unsigned)(*v - '0');
        }
        if (n < 5 || n > 60) return NTP_CFG_BAD_VALUE;
        cfg->timeout_s = n;
        return NTP_CFG_OK;
    }
    case K_SSID:
        return text(cfg->ssid, NTP_SSID_MAX, v, false);
    default:
        return password(cfg, v);
    }
}

/* The value after `=`, in place: quoted, up to its closing quote, with
 * only a comment after it; or bare, to a comment. */
static ntp_cfg_status_t value(char *v, char **out) {
    v = trim(v);
    if (*v != '"') {
        strip_comment(v);
        *out = trim(v);
        return NTP_CFG_OK;
    }
    char *close = strchr(v + 1, '"');
    if (!close) return NTP_CFG_SYNTAX;
    *close = 0;
    char *rest = trim(close + 1);
    if (*rest && *rest != '#') return NTP_CFG_SYNTAX;
    *out = v + 1;
    return NTP_CFG_OK;
}

/* One line, NUL-terminated and writable. */
static ntp_cfg_status_t parse_line(ntp_config_t *cfg, char *line, unsigned *seen) {
    char *t = trim(line);
    if (!*t || *t == '#') return NTP_CFG_OK;
    char *eq = strchr(t, '=');
    if (!eq) return NTP_CFG_SYNTAX;
    *eq = 0;
    char *name = trim(t), *v;
    if (!*name) return NTP_CFG_SYNTAX;
    ntp_cfg_status_t st = value(eq + 1, &v);
    if (st != NTP_CFG_OK) return st;
    for (unsigned k = 0; k < K_COUNT; k++) {
        if (!same_name(name, k_names[k])) continue;
        if (*seen & (1u << k)) return NTP_CFG_DUPLICATE;
        st = apply(cfg, k, v);
        if (st == NTP_CFG_OK) *seen |= 1u << k;
        return st;
    }
    return NTP_CFG_UNKNOWN;
}

ntp_cfg_status_t ntp_config_parse(ntp_config_t *cfg, const char *src, size_t len,
                                  unsigned *line) {
    char buf[NTP_CFG_LINE_MAX + 1];
    ntp_cfg_status_t first = NTP_CFG_OK;
    unsigned seen = 0, n_line = 0;
    defaults(cfg);
    *line = 0;
    for (size_t at = 0; at < len;) {
        n_line++;
        size_t end = at;
        while (end < len && src[end] != '\n') end++;
        size_t n = end - at;
        ntp_cfg_status_t st;
        if (n > NTP_CFG_LINE_MAX) {
            st = NTP_CFG_TOO_LONG;
        } else {
            memcpy(buf, src + at, n);
            buf[n] = 0;
            /* A NUL inside the line is not text. */
            st = strlen(buf) != n ? NTP_CFG_SYNTAX : parse_line(cfg, buf, &seen);
        }
        if (st != NTP_CFG_OK && first == NTP_CFG_OK) {
            first = st;
            *line = n_line;
        }
        at = end + 1;
    }
    if (first == NTP_CFG_OK && !cfg->ssid[0]) first = NTP_CFG_NO_SSID;
    return first;
}

const char *ntp_cfg_status_str(ntp_cfg_status_t st) {
    switch (st) {
    case NTP_CFG_OK:        return "ok";
    case NTP_CFG_SYNTAX:    return "not key = value";
    case NTP_CFG_UNKNOWN:   return "no such setting";
    case NTP_CFG_BAD_VALUE: return "no such value";
    case NTP_CFG_DUPLICATE: return "given twice";
    case NTP_CFG_TOO_LONG:  return "too long";
    case NTP_CFG_NO_SSID:   return "no ssid";
    }
    return "?";
}
