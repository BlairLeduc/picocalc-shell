/* test_parse.c — a line into commands (parse.h, design.md §4.4). */

#include <string.h>

#include "parse.h"
#include "test_util.h"

static parse_t p;

/* The commands as text: words joined by '|', redirections as <x >x >>x,
 * commands joined by their separators. */
static const char *show(void) {
    static char buf[1024];
    static const char *const seps[] = { "", " ; ", " && ", " || " };
    buf[0] = '\0';
    for (int i = 0; i < p.ncmds; i++) {
        const parse_cmd_t *c = &p.cmds[i];
        strcat(buf, seps[c->sep]);
        for (int j = 0; j < c->argc; j++) {
            if (j) strcat(buf, "|");
            strcat(buf, c->argv[j]);
        }
        if (c->argv[c->argc] != NULL) strcat(buf, "(argv not NULL-ended)");
        if (c->in) strcat(buf, " <"), strcat(buf, c->in);
        if (c->out) strcat(buf, c->append ? " >>" : " >"), strcat(buf, c->out);
    }
    return buf;
}

int main(void) {
    static const struct { const char *line, *want; } good[] = {
        { "",                          "" },
        { "   ",                       "" },
        { "# a comment",               "" },
        { "ls",                        "ls" },
        { "  ls  -l   /home ",         "ls|-l|/home" },
        { "echo 'a  b' \"c d\"",       "echo|a  b|c d" },
        { "echo it\\'s",               "echo|it's" },
        { "echo a\\ b",                "echo|a b" },
        { "echo 'x'\"y\"z",            "echo|xyz" },
        { "echo '' \"\"",              "echo||" },
        { "echo \"a\\\"b\\\\c\\d\"",   "echo|a\"b\\c\\d" },
        { "echo '\\'",                 "echo|\\" },
        { "echo a#b # rest",           "echo|a#b" },
        { "echo '#' \\#",              "echo|#|#" },
        { "echo $HOME",                "echo|$HOME" },
        { "echo '>' \\; \"&&\"",       "echo|>|;|&&" },
        { "ls > out",                  "ls >out" },
        { "ls >out",                   "ls >out" },
        { "ls>>out",                   "ls >>out" },
        { "cat < in > out",            "cat <in >out" },
        { "> out ls -a",               "ls|-a >out" },
        { "cat <'my file'",            "cat <my file" },
        { "a;b",                       "a ; b" },
        { "a ; b ;",                   "a ; b" },
        { "a && b || c",               "a && b || c" },
        { "a&&b||c;d",                 "a && b || c ; d" },
        { "mkdir x && cd x > log",     "mkdir|x && cd|x >log" },
    };
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        bool ok = parse_line(&p, good[i].line);
        CHECK(ok, "'%s': %s", good[i].line, p.error);
        if (ok)
            CHECK(strcmp(show(), good[i].want) == 0, "'%s' gave '%s', want '%s'", good[i].line,
                  show(), good[i].want);
    }

    static const char *const bad[] = {
        "echo 'a",
        "echo \"a",
        "echo a\\",
        "ls |more",
        "ls &",
        "; ls",
        "ls ;; ls",
        "ls &&",
        "ls ||",
        "&& ls",
        "ls >",
        "ls > ;",
        "ls < #x",
        "> out",
        "> out ; ls",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        bool ok = parse_line(&p, bad[i]);
        CHECK(!ok && p.error, "'%s' was taken: '%s'", bad[i], show());
    }

    /* The limits: SHELL_WORDS_MAX words, SHELL_CMDS_MAX commands. */
    char line[SHELL_LINE_MAX + 1] = "";
    for (unsigned i = 0; i < SHELL_WORDS_MAX; i++) strcat(line, "w ");
    CHECK(parse_line(&p, line) && p.cmds[0].argc == (int)SHELL_WORDS_MAX, "the most words");
    strcat(line, "w");
    CHECK(!parse_line(&p, line), "one word more");
    line[0] = '\0';
    for (unsigned i = 0; i < SHELL_CMDS_MAX; i++) strcat(line, "a;");
    CHECK(parse_line(&p, line) && p.ncmds == (int)SHELL_CMDS_MAX, "the most commands");
    strcat(line, "a");
    CHECK(!parse_line(&p, line), "one command more");

    /* The longest line of the shortest words fits the text buffer. */
    memset(line, 0, sizeof line);
    for (unsigned i = 0; i < SHELL_LINE_MAX; i++) line[i] = (i % 2) ? ';' : 'a';
    CHECK(!parse_line(&p, line) || p.ncmds <= (int)SHELL_CMDS_MAX, "no overrun");
    memset(line, 0, sizeof line);
    memset(line, 'q', SHELL_LINE_MAX);
    CHECK(parse_line(&p, line) && strlen(p.cmds[0].argv[0]) == SHELL_LINE_MAX, "one long word");
    static char longer[SHELL_LINE_MAX + 2];
    memset(longer, 'q', SHELL_LINE_MAX + 1);
    CHECK(!parse_line(&p, longer) && strcmp(p.error, "line too long") == 0, "one character more");

    TEST_DONE();
}
