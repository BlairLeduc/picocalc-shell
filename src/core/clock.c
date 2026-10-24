/* clock.c — the date and time (clock.h; design.md §3.8). */

#include "clock.h"

#include <string.h>

#include "ff.h"
#include "vars.h"

static shell_clock_t s_port;   /* the port's clock; get NULL: none */

void shell_set_clock(shell_t *sh, const shell_clock_t *clock) {
    (void)sh;
    s_port = *clock;
}

bool clock_utc(int64_t *utc_s) {
    int64_t ms;
    if (!s_port.get || !s_port.get(s_port.ctx, &ms)) return false;
    *utc_s = ms >= 0 ? ms / 1000 : -((999 - ms) / 1000);
    return true;
}

const char *clock_set(int64_t utc_s) {
    if (!s_port.set) return "no clock here";
    return s_port.set(s_port.ctx, utc_s * 1000);
}

const char *clock_note(void) {
    const char *note = s_port.note;
    s_port.note = NULL;
    return note;
}

void clock_leaving(void) {
    int64_t s;
    clock_tm_t tm;
    if (!s_port.offset || !clock_utc(&s)) return;
    clock_local(s, &tm);
    s_port.offset(s_port.ctx, tm.off_min);
}

bool clock_has_ntp(void) {
    return s_port.ntp && s_port.ntp_keep;
}

int clock_ntp(void) {
    return clock_has_ntp() ? s_port.ntp(s_port.ctx) : -1;
}

void clock_ntp_keep(int result) {
    if (clock_has_ntp()) s_port.ntp_keep(s_port.ctx, result);
}

/* ---- the calendar ----------------------------------------------------- */

static bool leap(int y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int clock_month_days(int year, int mon) {
    static const unsigned char days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return mon == 2 && leap(year) ? 29 : days[mon - 1];
}

bool clock_valid(int year, int mon, int mday) {
    return mon >= 1 && mon <= 12 && mday >= 1 && mday <= clock_month_days(year, mon);
}

/* Howard Hinnant's days_from_civil, for the proleptic Gregorian calendar. */
int64_t clock_days(int year, int mon, int mday) {
    int64_t y = year - (mon <= 2);
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (mon + (mon > 2 ? -3 : 9)) + 2) / 5 + mday - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

/* ...and its civil_from_days. */
static void civil(int64_t z, clock_tm_t *tm) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    tm->mday = (int)(doy - (153 * mp + 2) / 5 + 1);
    tm->mon = (int)(mp < 10 ? mp + 3 : mp - 9);
    tm->year = (int)(yoe + era * 400 + (tm->mon <= 2));
}

static int64_t floor_div(int64_t a, int64_t b) {
    return a >= 0 ? a / b : -((b - 1 - a) / b);
}

/* Seconds as if UTC, broken down; the zone is left alone. */
static void breakdown(int64_t s, clock_tm_t *tm) {
    int64_t days = floor_div(s, 86400);
    int64_t rest = s - days * 86400;
    civil(days, tm);
    tm->hour = (int)(rest / 3600);
    tm->min = (int)(rest / 60 % 60);
    tm->sec = (int)(rest % 60);
    tm->wday = (int)((days % 7 + 11) % 7);   /* 1970-01-01 was a Thursday */
}

int64_t clock_seconds(const clock_tm_t *tm) {
    return clock_days(tm->year, tm->mon, tm->mday) * 86400 + tm->hour * 3600 + tm->min * 60 +
           tm->sec;
}

const char *clock_wday_name(int wday) {
    static const char names[7][4] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return names[wday];
}

const char *clock_mon_name(int mon) {
    static const char names[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                       "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    return names[mon - 1];
}

/* ---- TZ --------------------------------------------------------------- */

/* When daylight saving starts or ends: Jn (1-365, no 29 February), n
 * (0-365) or Mm.w.d (day d of week w of month m, week 5 the last), at
 * `time` seconds of local time. */
typedef struct {
    char kind;          /* 'J', 'N' or 'M' */
    int  a, b, c;
    long time;
} tz_rule_t;

typedef struct {
    char      std[SHELL_ZONE_MAX + 1], dst[SHELL_ZONE_MAX + 1];
    long      std_west, dst_west;   /* seconds to add to local time for UTC */
    bool      has_dst;
    tz_rule_t start, end;
} tz_t;

static bool digit(char c) {
    return c >= '0' && c <= '9';
}

static bool number(const char **s, int lo, int hi, int *v) {
    if (!digit(**s)) return false;
    long n = 0;
    while (digit(**s)) {
        n = n * 10 + (*(*s)++ - '0');
        if (n > hi) return false;
    }
    *v = (int)n;
    return n >= lo;
}

/* A zone's name: three or more letters, or anything in <...>. */
static bool zone_name(const char **s, char *out) {
    const char *p = *s;
    size_t n = 0;
    if (*p == '<') {
        for (p++; *p && *p != '>'; p++)
            if (n < SHELL_ZONE_MAX) out[n++] = *p;
        if (*p++ != '>') return false;
    } else {
        for (; (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'); p++)
            if (n < SHELL_ZONE_MAX) out[n++] = *p;
    }
    out[n] = '\0';
    *s = p;
    return n >= 3;
}

/* [+-]hh[:mm[:ss]], up to `hours`. */
static bool hms(const char **s, int hours, long *secs) {
    int sign = 1, h, m = 0, sec = 0;
    if (**s == '+' || **s == '-') sign = *(*s)++ == '-' ? -1 : 1;
    if (!number(s, 0, hours, &h)) return false;
    if (**s == ':') {
        (*s)++;
        if (!number(s, 0, 59, &m)) return false;
        if (**s == ':') {
            (*s)++;
            if (!number(s, 0, 59, &sec)) return false;
        }
    }
    *secs = sign * ((long)h * 3600 + m * 60 + sec);
    return true;
}

static bool rule(const char **s, tz_rule_t *r) {
    r->time = 7200;
    if (**s == 'J') {
        (*s)++;
        r->kind = 'J';
        if (!number(s, 1, 365, &r->a)) return false;
    } else if (**s == 'M') {
        (*s)++;
        r->kind = 'M';
        if (!number(s, 1, 12, &r->a) || *(*s)++ != '.' || !number(s, 1, 5, &r->b) ||
            *(*s)++ != '.' || !number(s, 0, 6, &r->c))
            return false;
    } else {
        r->kind = 'N';
        if (!number(s, 0, 365, &r->a)) return false;
    }
    if (**s == '/') {
        (*s)++;
        return hms(s, 167, &r->time);
    }
    return true;
}

/* TZ read; false, and UTC, if it is not set or cannot be read. */
static bool tz_read(tz_t *z) {
    static const char us_rules[] = ",M3.2.0,M11.1.0";
    memset(z, 0, sizeof *z);
    strcpy(z->std, "UTC");
    const char *s = vars_get("TZ", 2);
    if (!s || !*s) return false;
    if (*s == ':') return false;   /* a zoneinfo file, which there is none of */
    if (!zone_name(&s, z->std) || !hms(&s, 24, &z->std_west)) goto bad;
    if (!*s) return true;
    if (!zone_name(&s, z->dst)) goto bad;
    z->has_dst = true;
    z->dst_west = z->std_west - 3600;
    if (*s && *s != ',' && !hms(&s, 24, &z->dst_west)) goto bad;
    if (!*s) s = us_rules;
    if (*s++ != ',' || !rule(&s, &z->start) || *s++ != ',' || !rule(&s, &z->end) || *s) goto bad;
    return true;
bad:
    memset(z, 0, sizeof *z);
    strcpy(z->std, "UTC");
    return false;
}

/* When a rule falls in a year, in local seconds as if UTC. */
static int64_t rule_at(int year, const tz_rule_t *r) {
    int64_t day = clock_days(year, 1, 1);
    switch (r->kind) {
    case 'J':
        day += r->a - 1 + (leap(year) && r->a >= 60);
        break;
    case 'N':
        day += r->a;
        break;
    default: {
        int64_t first = clock_days(year, r->a, 1);
        int wd = (int)((first % 7 + 11) % 7);
        int mday = 1 + (r->c - wd + 7) % 7 + (r->b - 1) * 7;
        if (mday > clock_month_days(year, r->a)) mday -= 7;
        day = first + mday - 1;
    }
    }
    return day * 86400 + r->time;
}

static bool in_dst(const tz_t *z, int64_t t) {
    if (!z->has_dst) return false;
    clock_tm_t tm;
    breakdown(t - z->std_west, &tm);
    int64_t start = rule_at(tm.year, &z->start) + z->std_west;
    int64_t end = rule_at(tm.year, &z->end) + z->dst_west;
    return start < end ? t >= start && t < end : t >= start || t < end;
}

void clock_gmt(int64_t utc_s, clock_tm_t *tm) {
    breakdown(utc_s, tm);
    tm->off_min = 0;
    tm->dst = false;
    strcpy(tm->zone, "UTC");
}

void clock_local(int64_t utc_s, clock_tm_t *tm) {
    tz_t z;
    tz_read(&z);
    bool dst = in_dst(&z, utc_s);
    long west = dst ? z.dst_west : z.std_west;
    breakdown(utc_s - west, tm);
    tm->off_min = (int)(-west / 60);
    tm->dst = dst;
    strcpy(tm->zone, dst ? z.dst : z.std);
}

int64_t clock_from_local(const clock_tm_t *tm) {
    tz_t z;
    tz_read(&z);
    int64_t local = clock_seconds(tm);
    if (z.has_dst && in_dst(&z, local + z.dst_west)) return local + z.dst_west;
    return local + z.std_west;
}

bool clock_now(clock_tm_t *tm) {
    int64_t t;
    if (clock_utc(&t)) {
        clock_local(t, tm);
        return true;
    }
    memset(tm, 0, sizeof *tm);
    tm->year = FF_NORTC_YEAR;
    tm->mon = FF_NORTC_MON;
    tm->mday = FF_NORTC_MDAY;
    tm->wday = (int)((clock_days(tm->year, tm->mon, tm->mday) % 7 + 11) % 7);
    clock_local(clock_from_local(tm), tm);
    return false;
}

/* ---- FAT -------------------------------------------------------------- */

void clock_to_fat(const clock_tm_t *tm, uint16_t *fdate, uint16_t *ftime) {
    *fdate = (uint16_t)((tm->year - 1980) << 9 | tm->mon << 5 | tm->mday);
    *ftime = (uint16_t)(tm->hour << 11 | tm->min << 5 | tm->sec / 2);
}

void clock_from_fat(uint16_t fdate, uint16_t ftime, clock_tm_t *tm) {
    memset(tm, 0, sizeof *tm);
    if (fdate == 0) return;
    tm->year = 1980 + (fdate >> 9);
    tm->mon = fdate >> 5 & 15;
    tm->mday = fdate & 31;
    tm->hour = ftime >> 11;
    tm->min = ftime >> 5 & 63;
    tm->sec = (ftime & 31) * 2;
}

/* FatFs's, for every file it writes: local time, as DOS and Windows
 * write it, or the fixed date. */
DWORD get_fattime(void) {
    clock_tm_t tm;
    clock_now(&tm);
    if (tm.year < 1980 || tm.year > 2107) {
        tm = (clock_tm_t){ .year = FF_NORTC_YEAR, .mon = FF_NORTC_MON, .mday = FF_NORTC_MDAY };
    }
    uint16_t d, t;
    clock_to_fat(&tm, &d, &t);
    return (DWORD)d << 16 | t;
}
