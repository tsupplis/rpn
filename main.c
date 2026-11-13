/*
 * main.c - command line front end for librpn.
 *
 * Everything about the input model lives here, not in the library:
 *
 *   - A line is executed when it ends (return key / newline).
 *   - A line whose last word is "_" is continued: nothing runs, the "_" is
 *     dropped and the next line is appended directly to it.  The "_" must
 *     stand alone: "x_" is a name, "x _" and "x_ _" continue.  A "_" inside
 *     a comment does not count, and a comment before the "_" is dropped.
 *   - A physical line is limited to 256 characters (including a trailing
 *     '_').  A longer line is rejected and any pending text is discarded.
 *     A joined (continued) line is limited to 4096 characters.
 *   - After a line has run, the top of the stack is echoed (nothing when
 *     the stack is empty, nor for blank lines, nor for a line that used p,
 *     nor for lines that are part of a program being entered).
 *   - "p" prints stack levels.  Its arguments are the literals written
 *     just before it; they are consumed and never pushed:
 *         p        top of stack          n p     level n (0 = top)
 *         * p      all levels            a b p   levels a to b, in the
 *                                                given direction
 *     Levels deeper than the stack are skipped.  "*" is the multiply
 *     command everywhere except directly before p.
 *
 *   - "exit" ends the run, using the top of the stack (rounded to an
 *     integer in 0..255, or 0 when the stack is empty) as the exit status.
 *     A value outside 0..255 is an error and the run continues.
 *   - "quit" ends the run with the normal exit status.
 *     Nothing after exit/quit on the same line or in later lines is run,
 *     and the line containing it does not echo the stack.
 *
 * Usage: rpn [-h] [-v] [-q] [-s N] [-l file] [-x file] [file ...]
 * Options are single letters and case-insensitive (CP/M-86 upper-cases the
 * command line).  File names are used as typed.
 *     -h  help        -v  version        -q  do not echo the top of stack
 *     -s N  maximum stack depth
 *     -l file  load: run the file silently, keep the state, carry on
 *     -x file  execute: run the file (with echo), then stop; stdin is not read
 * Files are run in command line order.  Without -x and without file names
 * (or with "-") the input is read from standard input.
 * Exit status: 0 success, 1 at least one line failed, 2 usage or I/O error.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rpn.h"

#define LINE_LIMIT 256
#define JOINED_LIMIT 4096
/* Room to see a trailing CR and one character past the limit. */
#define READ_CAP (LINE_LIMIT + 2)

typedef struct cli_t {
    rpn_ctx_t *calc;
    int quiet;
    int lineno;
    int errors;
    int done;            /* set by exit / quit                          */
    int code;            /* exit status requested by exit               */
    int has_code;
} cli_t;

/* One of the two most recent tokens, kept to recognise p arguments. */
typedef struct parg_t {
    int kind;            /* 0 other, 1 "*", 2 integer literal */
    long val;
    const char *start;   /* where the token begins in the line */
} parg_t;

/* ---- host environment -------------------------------------------- */

static void *env_alloc(void *user, size_t size)
{
    (void)user;
    return malloc(size);
}

static void env_release(void *user, void *ptr)
{
    (void)user;
    free(ptr);
}

static void env_out(void *user, const char *text)
{
    (void)user;
    fputs(text, stdout);
}

static void report(cli_t *cli, const char *msg)
{
    cli->errors++;
    fflush(stdout);
    fprintf(stderr, "rpn: line %d: %s\n", cli->lineno, msg);
}

static void env_error(void *user, int code, const char *message)
{
    (void)code;
    report((cli_t *)user, message);
}

/* ---- printing ----------------------------------------------------- */

static void print_level(rpn_ctx_t *c, size_t level)
{
    rpn_value_t v;
    char buf[RPN_FORMAT_MAX];

    if (rpn_peek(c, level, &v) != RPN_OK) {
        return;
    }
    if (rpn_format(c, &v, buf, sizeof buf) != 0) {
        strcpy(buf, "?");
    }
    puts(buf);
}

/* Prints levels a..b in the given direction, skipping levels too deep. */
static void print_range(rpn_ctx_t *c, size_t a, size_t b)
{
    size_t depth = rpn_depth(c);
    size_t i;

    if (depth == 0) {
        return;
    }
    if (a <= b) {
        if (b > depth - 1) {
            b = depth - 1;
        }
        for (i = a; i <= b; i++) {
            print_level(c, i);
        }
        return;
    }
    i = (a > depth - 1) ? depth - 1 : a;
    if (b > i) {
        return;
    }
    for (;;) {
        print_level(c, i);
        if (i == b) {
            break;
        }
        i--;
    }
}

/* ---- line execution ----------------------------------------------- */

/* Runs the text in [from, to) through the library. */
static int exec_range(cli_t *cli, const char *from, const char *to)
{
    char buf[JOINED_LIMIT + 1];
    size_t n = (size_t)(to - from);

    if (n == 0) {
        return RPN_OK;
    }
    memcpy(buf, from, n);
    buf[n] = '\0';
    return rpn_exec(cli->calc, buf);
}

/*
 * Takes the exit status from the top of the stack.  Returns 1 when it is
 * valid (the run can end) and 0 after reporting an error.
 */
static int do_exit(cli_t *cli)
{
    rpn_value_t top;

    cli->has_code = 1;
    cli->code = 0;
    if (rpn_peek(cli->calc, 0, &top) != RPN_OK) {
        return 1;
    }
    if (top.tag == RPN_T_INT && top.u.i >= 0 && top.u.i <= 255) {
        cli->code = (int)top.u.i;
        return 1;
    }
    if (top.tag == RPN_T_REAL && top.u.r >= 0.0 && top.u.r < 255.5) {
        cli->code = (int)(top.u.r + 0.5);
        return 1;
    }
    cli->has_code = 0;
    report(cli, "exit status must be a number from 0 to 255");
    return 0;
}

static void classify(parg_t *a, const char *tok, const char *end)
{
    rpn_value_t v;

    a->kind = 0;
    a->val = 0;
    a->start = end - strlen(tok);
    if (strcmp(tok, "*") == 0) {
        a->kind = 1;
    } else if (rpn_parse_number(tok, &v) == 1 && v.tag == RPN_T_INT) {
        a->kind = 2;
        a->val = v.u.i;
    }
}

/* Executes one logical line, handling the p pseudo-operator. */
static void run_line(cli_t *cli, const char *line)
{
    char tok[RPN_MAX_TOKEN + 1];
    parg_t a1, a2;
    const char *p = line;
    const char *seg = line;
    const char *argstart;
    long lo, hi;
    int all, r, ntok = 0, printed = 0, failed = 0;
    int was_prog = rpn_in_program(cli->calc);
    rpn_value_t top;
    char buf[RPN_FORMAT_MAX];

    a1.kind = a2.kind = 0;
    a1.val = a2.val = 0;
    a1.start = a2.start = NULL;

    for (;;) {
        r = rpn_next_token(&p, tok, sizeof tok);
        if (r == 0) {
            break;
        }
        ntok++;
        if (r == 1 && (strcmp(tok, "exit") == 0 ||
                       strcmp(tok, "quit") == 0)) {
            if (exec_range(cli, seg, p - strlen(tok)) != RPN_OK) {
                failed = 1;
                break;
            }
            if (tok[0] == 'q' || do_exit(cli)) {
                cli->done = 1;
            }
            return;
        }
        if (r == 1 && strcmp(tok, "p") == 0) {
            all = 0;
            lo = hi = 0;
            argstart = p - 1;
            if (a1.kind == 1) {
                all = 1;
                argstart = a1.start;
            } else if (a1.kind == 2 && a2.kind == 2) {
                lo = a2.val;
                hi = a1.val;
                argstart = a2.start;
            } else if (a1.kind == 2) {
                lo = hi = a1.val;
                argstart = a1.start;
            }
            if (exec_range(cli, seg, argstart) != RPN_OK) {
                failed = 1;
                break;
            }
            if (rpn_in_program(cli->calc)) {
                report(cli, "p cannot be stored in a program");
                failed = 1;
                break;
            }
            if (lo < 0 || hi < 0) {
                report(cli, "bad argument for p (levels start at 0)");
                failed = 1;
                break;
            }
            if (all) {
                print_range(cli->calc, 0, rpn_depth(cli->calc));
            } else {
                print_range(cli->calc, (size_t)lo, (size_t)hi);
            }
            printed = 1;
            seg = p;
            a1.kind = a2.kind = 0;
            continue;
        }
        a2 = a1;
        if (r == 1) {
            classify(&a1, tok, p);
        } else {
            a1.kind = 0;
            a1.val = 0;
            a1.start = NULL;
        }
    }
    if (!failed && exec_range(cli, seg, p) != RPN_OK) {
        failed = 1;
    }
    if (!cli->quiet && ntok > 0 && !printed && !was_prog &&
        !rpn_in_program(cli->calc) &&
        rpn_peek(cli->calc, 0, &top) == RPN_OK) {
        if (rpn_format(cli->calc, &top, buf, sizeof buf) != 0) {
            strcpy(buf, "?");
        }
        puts(buf);
    }
}

/* ---- line assembly ------------------------------------------------ */

/*
 * Reads one physical line (without its newline) into buf, which must hold
 * READ_CAP + 1 bytes.  Sets *toolong when it exceeds LINE_LIMIT.  A CR
 * before the newline is ignored and NUL bytes become spaces.  A Ctrl-Z
 * (0x1A, the CP/M and DOS end-of-file mark) ends the input.
 * Returns 0 at end of input.
 */
static int read_line(FILE *f, char *buf, int *toolong)
{
    int ch;
    size_t total = 0;
    size_t n = 0;
    int any = 0;

    while ((ch = getc(f)) != EOF && ch != 0x1A && ch != '\n') {
        any = 1;
        if (ch == '\0') {
            ch = ' ';
        }
        if (n < READ_CAP) {
            buf[n++] = (char)ch;
        }
        total++;
    }
    if ((ch == EOF || ch == 0x1A) && !any) {
        return 0;
    }
    if (total > 0 && total <= READ_CAP && buf[n - 1] == '\r') {
        total--;
        n--;
    }
    buf[n] = '\0';
    *toolong = total > LINE_LIMIT;
    return 1;
}

/*
 * Returns the offset of the continuation mark of a line, or -1.  The mark
 * is a "_" that is the last word of the line, before any comment.
 */
static int cont_mark(const char *line)
{
    size_t end = 0;

    while (line[end] != '\0' && line[end] != '#') {
        end++;
    }
    while (end > 0 && isspace((unsigned char)line[end - 1])) {
        end--;
    }
    if (end == 0 || line[end - 1] != '_') {
        return -1;
    }
    if (end > 1 && !isspace((unsigned char)line[end - 2])) {
        return -1;
    }
    return (int)(end - 1);
}

static void process(cli_t *cli, FILE *f)
{
    char line[READ_CAP + 1];
    char acc[JOINED_LIMIT + 1];
    size_t acc_len = 0;
    size_t n;
    int pending = 0;
    int continued;
    int mark;
    int toolong;

    while (!cli->done && read_line(f, line, &toolong)) {
        cli->lineno++;
        if (toolong) {
            report(cli, "line longer than 256 characters");
            pending = 0;
            acc_len = 0;
            continue;
        }
        mark = cont_mark(line);
        continued = (mark >= 0);
        n = continued ? (size_t)mark : strlen(line);
        if (acc_len + n > JOINED_LIMIT) {
            report(cli, "continued line longer than 4096 characters");
            pending = 0;
            acc_len = 0;
            continue;
        }
        memcpy(acc + acc_len, line, n);
        acc_len += n;
        acc[acc_len] = '\0';
        if (continued) {
            pending = 1;
            continue;
        }
        pending = 0;
        acc_len = 0;
        run_line(cli, acc);
    }
    if (pending && !cli->done) {
        report(cli, "input ended inside a continued line (discarded)");
    }
}

/* ---- main --------------------------------------------------------- */

static void usage(FILE *to)
{
    fputs("usage: rpn [-h] [-v] [-q] [-s N] [-l file] [-x file] [file ...]\n"
          "  -h       show this text\n"
          "  -v       show the version\n"
          "  -q       do not echo the top of the stack after each line\n"
          "  -s N     maximum stack depth (default 100)\n"
          "  -l file  load a file silently and carry on\n"
          "  -x file  execute a file, then stop (stdin is not read)\n"
          "Options are case-insensitive.\n"
          "Commands exit (status = top of stack, 0..255) and quit end the run.\n"
          "Reads standard input when no file (or \"-\") is given.\n", to);
}

/* Returns the lower-case letter of an option such as "-Q", else 0. */
static int opt_of(const char *a)
{
    if (a[0] == '-' && a[1] != '\0' && a[2] == '\0') {
        return tolower((unsigned char)a[1]);
    }
    return 0;
}

/* Runs a file; silent turns the echo off for it.  Returns 0, or -1 if the
 * file cannot be opened. */
static int run_file(cli_t *cli, const char *name, int silent)
{
    FILE *f = fopen(name, "r");
    int saved = cli->quiet;

    if (f == NULL) {
        fprintf(stderr, "rpn: cannot open '%s'\n", name);
        return -1;
    }
    if (silent) {
        cli->quiet = 1;
    }
    cli->lineno = 0;
    process(cli, f);
    cli->quiet = saved;
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    cli_t cli;
    rpn_env_t env;
    char *end;
    long max = 0;
    int i, o, ran = 0, stop = 0, status = 0;

    memset(&cli, 0, sizeof cli);

    /* pass 1: options that act at once or configure the run */
    for (i = 1; i < argc; i++) {
        o = opt_of(argv[i]);
        if (o == 'h') {
            usage(stdout);
            return 0;
        } else if (o == 'v') {
            puts("rpn " RPN_VERSION);
            return 0;
        } else if (o == 'q') {
            cli.quiet = 1;
        } else if (o == 's') {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
            max = strtol(argv[i], &end, 10);
            if (*argv[i] == '\0' || *end != '\0' || max < 1) {
                fprintf(stderr, "rpn: bad stack size '%s'\n", argv[i]);
                return 2;
            }
        } else if (o == 'l' || o == 'x') {
            if (++i >= argc) {
                usage(stderr);
                return 2;
            }
        } else if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr, "rpn: unknown option '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
    }

    env.user = &cli;
    env.alloc = env_alloc;
    env.release = env_release;
    env.out = env_out;
    env.error = env_error;
    cli.calc = rpn_new(&env);
    if (cli.calc == NULL) {
        fputs("rpn: out of memory\n", stderr);
        return 2;
    }
    if (max > 0) {
        rpn_set_stack_max(cli.calc, (size_t)max);
    }

    /* pass 2: run -l, -x and the plain file names in command line order */
    for (i = 1; i < argc && status == 0 && !stop && !cli.done; i++) {
        o = opt_of(argv[i]);
        if (o == 'q' || o == 'h' || o == 'v') {
            continue;
        }
        if (o == 's') {
            i++;
        } else if (o == 'l' || o == 'x') {
            i++;
            if (run_file(&cli, argv[i], o == 'l') != 0) {
                status = 2;
            }
            if (o == 'x') {
                stop = 1;
            }
        } else if (strcmp(argv[i], "-") == 0) {
            process(&cli, stdin);
            ran = 1;
        } else {
            if (run_file(&cli, argv[i], 0) != 0) {
                status = 2;
            }
            ran = 1;
        }
    }
    if (status == 0 && !stop && !ran && !cli.done) {
        process(&cli, stdin);
    }
    fflush(stdout);
    rpn_free(cli.calc);
    if (status == 0 && cli.has_code) {
        status = cli.code;
    } else if (status == 0 && cli.errors > 0) {
        status = 1;
    }
    return status;
}
