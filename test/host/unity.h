/* unity.h — the part of Unity that Pico Logo's editor tests use, so they
 * run here as they were written (design.md §4.6, THIRD-PARTY.md).
 *
 * Not Unity: no framework, as in EL §11.1. A failed assertion prints where
 * and why, and leaves its test at once, as Unity's does; the next test
 * still runs. UNITY_END is the exit status: 0, or 1 after any failure. */
#ifndef SHELL_UNITY_H
#define SHELL_UNITY_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

void setUp(void);
void tearDown(void);

static jmp_buf unity_jump;
static int unity_tests, unity_failed;

static inline void unity_fail(int line, const char *what, const char *msg) {
    fprintf(stderr, "%s:%d: FAIL %s%s%s\n", __FILE__, line, what, msg ? ": " : "", msg ? msg : "");
    longjmp(unity_jump, 1);
}

static inline void unity_int(long long want, long long got, int line, const char *msg) {
    if (want == got) return;
    char what[96];
    snprintf(what, sizeof what, "expected %lld, was %lld", want, got);
    unity_fail(line, what, msg);
}

static inline void unity_uint(unsigned long long want, unsigned long long got, int line,
                              const char *msg) {
    if (want == got) return;
    char what[96];
    snprintf(what, sizeof what, "expected %llu, was %llu", want, got);
    unity_fail(line, what, msg);
}

static inline void unity_string(const char *want, const char *got, int line, const char *msg) {
    if (want && got && strcmp(want, got) == 0) return;
    if (!want && !got) return;
    fprintf(stderr, "%s:%d: FAIL expected \"%s\"\n    was \"%s\"%s%s\n", __FILE__, line,
            want ? want : "(null)", got ? got : "(null)", msg ? ": " : "", msg ? msg : "");
    longjmp(unity_jump, 1);
}

#define TEST_ASSERT_TRUE_MESSAGE(c, m) \
    do { if (!(c)) unity_fail(__LINE__, "expected true: " #c, (m)); } while (0)
#define TEST_ASSERT_TRUE(c)  TEST_ASSERT_TRUE_MESSAGE(c, NULL)
#define TEST_ASSERT_FALSE(c) \
    do { if (c) unity_fail(__LINE__, "expected false: " #c, NULL); } while (0)
#define TEST_FAIL_MESSAGE(m) unity_fail(__LINE__, "failed", (m))

#define TEST_ASSERT_EQUAL_INT_MESSAGE(w, g, m) unity_int((long long)(w), (long long)(g), __LINE__, (m))
#define TEST_ASSERT_EQUAL_INT(w, g)            TEST_ASSERT_EQUAL_INT_MESSAGE(w, g, NULL)
#define TEST_ASSERT_EQUAL_CHAR_MESSAGE(w, g, m) TEST_ASSERT_EQUAL_INT_MESSAGE(w, g, m)
#define TEST_ASSERT_EQUAL_CHAR(w, g)           TEST_ASSERT_EQUAL_INT_MESSAGE(w, g, NULL)
#define TEST_ASSERT_NOT_EQUAL(w, g) \
    do { if ((w) == (g)) unity_fail(__LINE__, "expected unequal: " #w ", " #g, NULL); } while (0)
#define TEST_ASSERT_EQUAL_UINT_MESSAGE(w, g, m) \
    unity_uint((unsigned long long)(w), (unsigned long long)(g), __LINE__, (m))
#define TEST_ASSERT_EQUAL_UINT(w, g) TEST_ASSERT_EQUAL_UINT_MESSAGE(w, g, NULL)
#define TEST_ASSERT_EQUAL_UINT64(w, g) TEST_ASSERT_EQUAL_UINT_MESSAGE(w, g, NULL)
#define TEST_ASSERT_EQUAL_STRING_MESSAGE(w, g, m) unity_string((w), (g), __LINE__, (m))
#define TEST_ASSERT_EQUAL_STRING(w, g)            TEST_ASSERT_EQUAL_STRING_MESSAGE(w, g, NULL)

#define UNITY_BEGIN() (unity_tests = 0, unity_failed = 0)
#define RUN_TEST(fn)                                                       \
    do {                                                                   \
        unity_tests++;                                                     \
        if (setjmp(unity_jump) == 0) {                                     \
            setUp();                                                       \
            fn();                                                          \
        } else {                                                           \
            fprintf(stderr, "    in %s\n", #fn);                           \
            unity_failed++;                                                \
        }                                                                  \
        tearDown();                                                        \
    } while (0)
#define UNITY_END()                                                        \
    (printf("%d tests, %d failed\n", unity_tests, unity_failed), unity_failed ? 1 : 0)

#endif /* SHELL_UNITY_H */
