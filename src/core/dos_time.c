/* dos_time.c — the DOS flavour's date and time (dos.h; design.md §3.8):
 * DATE, TIME, and COUNTRY= in C:\CONFIG.SYS, which says how they and
 * DIR write a date, a time and a number.
 *
 * The countries' formats are MS-DOS 6's (its HELP COUNTRY), with the
 * separators FreeDOS's COUNTRY.SYS gives for the same codes. */

#include <stdio.h>
#include <string.h>

#include "clock.h"
#include "dos.h"
#include "files.h"
#include "line.h"

static const dos_country_t s_countries[] = {
    {   1, 'M', '-', ':', false, ',', '.' },   /* United States */
    {   2, 'Y', '-', ':', true,  ' ', ',' },   /* Canadian-French */
    {  33, 'D', '.', ':', true,  ' ', ',' },   /* France */
    {  44, 'D', '/', ':', true,  ',', '.' },   /* United Kingdom */
    {  49, 'D', '.', ':', true,  '.', ',' },   /* Germany */
    {  81, 'Y', '-', ':', true,  ',', '.' },   /* Japan */
};

static const dos_country_t *s_country = &s_countries[0];

const dos_country_t *dos_country(void) {
    return s_country;
}

bool dos_set_country(unsigned code) {
    for (size_t i = 0; i < sizeof s_countries / sizeof s_countries[0]; i++) {
        if (s_countries[i].code == code) {
            s_country = &s_countries[i];
            return true;
        }
    }
    return false;
}

static char upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

static bool blank(char c) {
    return c == ' ' || c == '\t';
}

static bool digit(char c) {
    return c >= '0' && c <= '9';
}

/* ---- CONFIG.SYS ------------------------------------------------------- */

#define CONFIG "/CONFIG.SYS"

/* What DOS's CONFIG.SYS set that means nothing here: passed over, so one
 * from an old PC does not complain. */
static const char *const s_ignored[] = {
    "FILES", "BUFFERS", "DEVICE", "DEVICEHIGH", "DOS", "LASTDRIVE", "BREAK", "SHELL",
    "INSTALL", "FCBS", "STACKS",
};

/* Whether s, to '=' or a blank, is `word`, without regard to case. */
static bool keyword(const char *s, const char *word, const char **rest) {
    for (; *word; s++, word++)
        if (upper(*s) != *word) return false;
    while (blank(*s)) s++;
    if (*s != '=' && *s != '\0') return false;
    *rest = *s == '=' ? s + 1 : s;
    return true;
}

static void config_error(shell_t *sh, const char *why, unsigned line) {
    shell_error(sh, "%s\nError in CONFIG.SYS line %u\n", why, line);
}

/* COUNTRY=nnn[,ppp[,file]]: only the code is read. */
static bool config_country(const char *s) {
    while (blank(*s)) s++;
    unsigned code = 0, digits = 0;
    for (; digit(*s) && digits < 4; s++, digits++) code = code * 10u + (unsigned)(*s - '0');
    while (blank(*s)) s++;
    if (!digits || (*s && *s != ',')) return false;
    return dos_set_country(code);
}

void dos_config(shell_t *sh) {
    static FIL fil;
    static reader_t rd;
    static char text[SHELL_TEXT_LINE];
    if (files_open_startup(&fil, CONFIG) != FR_OK) return;
    reader_init(&rd, &(input_t){ &fil, NULL });
    bool whole;
    unsigned n = 0;
    while (reader_line(&rd, text, sizeof text, &whole) >= 0) {
        while (!whole && reader_line(&rd, text, sizeof text, &whole) >= 0) {}
        n++;
        const char *s = text, *rest;
        while (blank(*s)) s++;
        bool rem = upper(s[0]) == 'R' && upper(s[1]) == 'E' && upper(s[2]) == 'M' &&
                   (!s[3] || blank(s[3]));
        if (!*s || *s == ';' || rem) continue;
        if (keyword(s, "COUNTRY", &rest)) {
            if (!config_country(rest)) config_error(sh, "Invalid country code or code page", n);
            continue;
        }
        bool known = false;
        for (size_t i = 0; i < sizeof s_ignored / sizeof s_ignored[0] && !known; i++)
            known = keyword(s, s_ignored[i], &rest);
        if (!known) config_error(sh, "Unrecognized command in CONFIG.SYS", n);
    }
    f_close(&fil);
}

/* ---- formats ---------------------------------------------------------- */

void dos_format_date(char *buf, size_t size, const clock_tm_t *tm, bool long_year) {
    const dos_country_t *c = s_country;
    int y = long_year ? tm->year : tm->year % 100;
    int w = long_year ? 4 : 2;
    if (c->order == 'M')
        snprintf(buf, size, "%02d%c%02d%c%0*d", tm->mon, c->date_sep, tm->mday, c->date_sep, w, y);
    else if (c->order == 'D')
        snprintf(buf, size, "%02d%c%02d%c%0*d", tm->mday, c->date_sep, tm->mon, c->date_sep, w, y);
    else
        snprintf(buf, size, "%0*d%c%02d%c%02d", w, y, c->date_sep, tm->mon, c->date_sep, tm->mday);
}

void dos_format_time(char *buf, size_t size, const clock_tm_t *tm, bool seconds) {
    const dos_country_t *c = s_country;
    int h = tm->hour;
    if (!c->h24) h = h % 12 ? h % 12 : 12;
    int n = snprintf(buf, size, "%2d%c%02d", h, c->time_sep, tm->min);
    if (seconds && n > 0 && (size_t)n < size)
        n += snprintf(buf + n, size - (size_t)n, "%c%02d%c00", c->time_sep, tm->sec, c->decimal);
    if (!c->h24 && n > 0 && (size_t)n < size)
        snprintf(buf + n, size - (size_t)n, "%c", tm->hour < 12 ? 'a' : 'p');
}

/* ---- DATE, TIME ------------------------------------------------------- */

/* Up to `max` digits, at least one. */
static bool read_num(const char **s, unsigned max, int *v, unsigned *digits) {
    *v = 0;
    *digits = 0;
    while (digit(**s) && *digits < max) {
        *v = *v * 10 + (*(*s)++ - '0');
        (*digits)++;
    }
    return *digits > 0;
}

static bool date_sep(char c) {
    return c == '-' || c == '/' || c == '.';
}

/* A date in the country's order, with any of - / . between. A year of
 * two digits is 1980-2079: 80-99 the 1900s, the rest the 2000s, as DOS
 * took them; DOS's range is 1980-2099. */
static bool parse_date(const char *s, clock_tm_t *tm) {
    int v[3];
    unsigned digits[3];
    while (blank(*s)) s++;
    for (int i = 0; i < 3; i++) {
        if (i > 0 && !date_sep(*s++)) return false;
        if (!read_num(&s, 4, &v[i], &digits[i])) return false;
    }
    while (blank(*s)) s++;
    if (*s) return false;
    int yi = s_country->order == 'Y' ? 0 : 2;
    int mi = s_country->order == 'D' ? 1 : s_country->order == 'M' ? 0 : 1;
    int di = s_country->order == 'D' ? 0 : s_country->order == 'M' ? 1 : 2;
    int y = v[yi];
    if (digits[yi] <= 2) y += y >= 80 ? 1900 : 2000;
    else if (digits[yi] != 4) return false;
    if (digits[mi] > 2 || digits[di] > 2) return false;
    if (y < 1980 || y > 2099 || !clock_valid(y, v[mi], v[di])) return false;
    tm->year = y;
    tm->mon = v[mi];
    tm->mday = v[di];
    return true;
}

/* hh[:mm[:ss[.xx]]][a|p|am|pm], with : or . between, as DOS took it. */
static bool parse_time(const char *s, clock_tm_t *tm) {
    int v[4] = { 0, 0, 0, 0 };
    unsigned digits;
    int n = 0;
    while (blank(*s)) s++;
    if (!read_num(&s, 2, &v[n++], &digits)) return false;
    while (n < 4 && (*s == ':' || *s == '.' || *s == ',' || *s == s_country->time_sep) &&
           digit(s[1])) {
        s++;
        if (!read_num(&s, 2, &v[n++], &digits)) return false;
    }
    while (blank(*s)) s++;
    char half = upper(*s);
    if (half == 'A' || half == 'P') {
        s++;
        if (upper(*s) == 'M') s++;
        if (v[0] < 1 || v[0] > 12) return false;
        v[0] = v[0] % 12 + (half == 'P' ? 12 : 0);
    } else {
        half = 0;
    }
    while (blank(*s)) s++;
    if (*s || v[0] > 23 || v[1] > 59 || v[2] > 59) return false;
    tm->hour = v[0];
    tm->min = v[1];
    tm->sec = v[2];
    return true;
}

static bool set(shell_t *sh, const clock_tm_t *tm) {
    const char *why = clock_set(clock_from_local(tm));
    if (why) dos_error(sh, why, NULL);
    return !why;
}

/* DATE and TIME: show, then ask until a good answer or Enter. The words
 * after the command are one date or time, as DOS read its tail, so
 * TIME 2:30 pm works. */
static int date_time(shell_t *sh, int argc, char **argv, bool is_date) {
    unsigned flags;
    if (!dos_switches(sh, &argc, argv, "T", false, &flags)) return 1;
    if (flags && dos_too_many(sh, argc, argv, 1)) return 1;
    const char *what = is_date ? "date" : "time";
    bool (*parse)(const char *, clock_tm_t *) = is_date ? parse_date : parse_time;
    clock_tm_t tm;
    bool was_set = clock_now(&tm);
    if (argc >= 2) {
        char given[SHELL_LINE_MAX + 1];
        size_t n = 0;
        for (int i = 1; i < argc; i++)
            n += (size_t)snprintf(given + n, n < sizeof given ? sizeof given - n : 0, "%s%s",
                                  i > 1 ? " " : "", argv[i]);
        if (n < sizeof given && parse(given, &tm)) return set(sh, &tm) ? 0 : 1;
        dos_error(sh, is_date ? "Invalid date" : "Invalid time", NULL);
    } else if (!was_set) {
        out_printf(&sh->out, "Current %s is not set\n", what);
    } else {
        char buf[24];
        if (is_date) {
            dos_format_date(buf, sizeof buf, &tm, true);
            out_printf(&sh->out, "Current date is %s %s\n", clock_wday_name(tm.wday), buf);
        } else {
            dos_format_time(buf, sizeof buf, &tm, true);
            out_printf(&sh->out, "Current time is %s\n", buf);
        }
    }
    if (flags) return was_set ? 0 : 1;
    if (!sh->hooks.getkey) return was_set ? 0 : 1;
    char prompt[40];
    if (is_date) {
        char c = s_country->date_sep;
        const char *f = s_country->order == 'M' ? "mm%cdd%cyy"
                        : s_country->order == 'D' ? "dd%cmm%cyy" : "yy%cmm%cdd";
        char fmt[16];
        snprintf(fmt, sizeof fmt, f, c, c);
        snprintf(prompt, sizeof prompt, "Enter new date (%s): ", fmt);
    } else {
        strcpy(prompt, "Enter new time: ");
    }
    for (;;) {
        int got = line_read(sh, prompt);
        if (got <= 0) return 1;
        const char *s = sh->line.text;
        while (blank(*s)) s++;
        if (!*s) return was_set ? 0 : 1;
        clock_now(&tm);
        if (parse(s, &tm)) return set(sh, &tm) ? 0 : 1;
        dos_error(sh, is_date ? "Invalid date" : "Invalid time", NULL);
    }
}

int dos_date(shell_t *sh, int argc, char **argv) {
    return date_time(sh, argc, argv, true);
}

int dos_time(shell_t *sh, int argc, char **argv) {
    return date_time(sh, argc, argv, false);
}
