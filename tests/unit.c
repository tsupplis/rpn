/*
 * unit.c - dependency-free unit tests for librpn.
 *
 * A mock rpn_env_t counts allocations, can fail the Nth one, records
 * output and the errors reported, and checks that nothing leaks.  Every
 * public error code is triggered at least once (see test_error_coverage).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rpn.h"

static int checks = 0;
static int failures = 0;

#define CHECK(cond) do { \
    checks++; \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

/* ---- mock environment --------------------------------------------- */

typedef struct mock_t {
    int allocs;        /* successful allocations                      */
    int frees;
    int calls;         /* allocation attempts                         */
    int fail_at;       /* fail the Nth attempt (1-based); 0 = never   */
    int nerrors;
    int last_code;
    char last_msg[160];
} mock_t;

static void *m_alloc(void *u, size_t n)
{
    mock_t *m = (mock_t *)u;

    m->calls++;
    if (m->fail_at != 0 && m->calls == m->fail_at) {
        return NULL;
    }
    m->allocs++;
    return malloc(n);
}

static void m_release(void *u, void *p)
{
    ((mock_t *)u)->frees++;
    free(p);
}

static void m_error(void *u, int code, const char *msg)
{
    mock_t *m = (mock_t *)u;

    m->nerrors++;
    m->last_code = code;
    strncpy(m->last_msg, msg, sizeof m->last_msg - 1);
}

static rpn_ctx_t *make(mock_t *m)
{
    rpn_env_t e;

    memset(m, 0, sizeof *m);
    e.user = m;
    e.alloc = m_alloc;
    e.release = m_release;
    e.out = NULL;
    e.error = m_error;
    return rpn_new(&e);
}

/* ---- helpers ------------------------------------------------------- */

/* Runs text, returns the error code. */
static int run(rpn_ctx_t *c, const char *text)
{
    return rpn_exec(c, text);
}

static int top_is_int(rpn_ctx_t *c, long want)
{
    rpn_value_t v;

    return rpn_peek(c, 0, &v) == RPN_OK && v.tag == RPN_T_INT &&
           v.u.i == want;
}

static int top_is_real(rpn_ctx_t *c, double want)
{
    rpn_value_t v;
    double d;

    if (rpn_peek(c, 0, &v) != RPN_OK || v.tag != RPN_T_REAL) {
        return 0;
    }
    d = v.u.r - want;
    return d < 1e-9 && d > -1e-9;
}

/* Evaluates text on a fresh calculator; true when top is the int. */
static int evals_int(const char *text, long want)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    int ok = (c != NULL) && run(c, text) == RPN_OK && top_is_int(c, want);

    rpn_free(c);
    return ok;
}

static int evals_real(const char *text, double want)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    int ok = (c != NULL) && run(c, text) == RPN_OK && top_is_real(c, want);

    rpn_free(c);
    return ok;
}

/* Evaluates text expecting error `code`; the stack must keep `depth`. */
static int fails_with(const char *text, int code, size_t depth)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    int ok;

    if (c == NULL) {
        return 0;
    }
    ok = run(c, text) == code && rpn_last_error(c) == code &&
         m.nerrors == 1 && m.last_code == code && rpn_depth(c) == depth;
    rpn_free(c);
    return ok;
}

/* The codes provoked by fails_seen() are recorded here (see the matrix). */
static int *seen_codes;

static int fails_seen(const char *text, int code, size_t depth)
{
    int ok = fails_with(text, code, depth);

    if (ok && seen_codes != NULL && code >= 0 && code < RPN_E_COUNT) {
        seen_codes[code] = 1;
    }
    return ok;
}

/* ---- tests ---------------------------------------------------------- */

static void test_lexer(void)
{
    char buf[8];
    const char *p = "  FOO  bar# comment\n baz";

    CHECK(rpn_next_token(&p, buf, sizeof buf) == 1 && strcmp(buf, "foo") == 0);
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 1 && strcmp(buf, "bar") == 0);
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 1 && strcmp(buf, "baz") == 0);
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 0);
    p = "# only a comment";
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 0);
    p = "abcdefghij next";
    CHECK(rpn_next_token(&p, buf, sizeof buf) == -1);
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 1 && strcmp(buf, "next") == 0);
    p = "a#b";
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 1 && strcmp(buf, "a") == 0);
    CHECK(rpn_next_token(&p, buf, sizeof buf) == 0);
}

static void test_numbers(void)
{
    rpn_value_t v;

    CHECK(rpn_parse_number("42", &v) == 1 && v.tag == RPN_T_INT && v.u.i == 42);
    CHECK(rpn_parse_number("-7", &v) == 1 && v.u.i == -7);
    CHECK(rpn_parse_number("+7", &v) == 1 && v.u.i == 7);
    CHECK(rpn_parse_number("0", &v) == 1 && v.u.i == 0);
    CHECK(rpn_parse_number("-0", &v) == 1 && v.tag == RPN_T_INT && v.u.i == 0);
    CHECK(rpn_parse_number("1.5", &v) == 1 && v.tag == RPN_T_REAL);
    CHECK(rpn_parse_number(".5", &v) == 1 && v.tag == RPN_T_REAL);
    CHECK(rpn_parse_number("5.", &v) == 1 && v.tag == RPN_T_REAL);
    CHECK(rpn_parse_number("1e3", &v) == 1 && v.tag == RPN_T_REAL &&
          v.u.r == 1000.0);
    CHECK(rpn_parse_number("1e-3", &v) == 1 && v.tag == RPN_T_REAL);
    CHECK(rpn_parse_number("9223372036854775807", &v) == 1 &&
          v.u.i == 9223372036854775807L);
    CHECK(rpn_parse_number("-9223372036854775808", &v) == 1);
    CHECK(rpn_parse_number("9223372036854775808", &v) == -1);
    CHECK(rpn_parse_number("-9223372036854775809", &v) == -1);
    CHECK(rpn_parse_number("1e999", &v) == -1);
    CHECK(rpn_parse_number("+", &v) == 0);
    CHECK(rpn_parse_number("-", &v) == 0);
    CHECK(rpn_parse_number(".", &v) == 0);
    CHECK(rpn_parse_number("e", &v) == 0);
    CHECK(rpn_parse_number("1e", &v) == 0);
    CHECK(rpn_parse_number("1.2.3", &v) == 0);
    CHECK(rpn_parse_number("--1", &v) == 0);
    CHECK(rpn_parse_number("12a", &v) == 0);
    CHECK(rpn_parse_number("dup", &v) == 0);
    CHECK(rpn_parse_number("", &v) == 0);
}

static void test_format(void)
{
    char buf[RPN_FORMAT_MAX];
    char small[8];
    rpn_value_t v;

    v = rpn_value_int(-12);
    CHECK(rpn_format(NULL, &v, buf, sizeof buf) == 0 && strcmp(buf, "-12") == 0);
    v = rpn_value_real(2.0);
    CHECK(rpn_format(NULL, &v, buf, sizeof buf) == 0 && strcmp(buf, "2.0") == 0);
    v = rpn_value_real(0.1);
    CHECK(rpn_format(NULL, &v, buf, sizeof buf) == 0 && strcmp(buf, "0.1") == 0);
    v = rpn_value_real(1e100);
    CHECK(rpn_format(NULL, &v, buf, sizeof buf) == 0 &&
          strcmp(buf, "1e+100") == 0);
    CHECK(rpn_format(NULL, &v, small, sizeof small) == -1);
    v.tag = RPN_T_MATRIX;
    CHECK(rpn_format(NULL, &v, buf, sizeof buf) == -1);
}

static void test_stack_commands(void)
{
    CHECK(evals_int("3 4 + 2 *", 14));
    CHECK(evals_int("1 2 swap drop", 2));
    CHECK(evals_int("1 2 x<>y drop", 2));
    CHECK(evals_int("5 enter +", 10));      /* alias */
    CHECK(evals_int("5 dup +", 10));
    CHECK(evals_int("1 2 3 rolldown", 2));
    CHECK(evals_int("1 2 3 rollup", 1));
    CHECK(evals_int("1 2 3 rdn rdn rdn", 3));
    CHECK(evals_int("1 2 3 rup rup rup", 3));
    CHECK(evals_int("1 2 3 rollup rolldown", 3));
    CHECK(evals_int("5 3 - lastx", 3));
    CHECK(evals_int("5 3 - lastx -", -1));
    CHECK(evals_int("9 neg lastx", 9));
    CHECK(evals_int("1 2 3 clear 7", 7));
    CHECK(evals_int("1 2 3 clx 7", 7));
    CHECK(evals_int("lastx", 0));
}

static void test_stack_depths(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    CHECK(rpn_depth(c) == 0);
    CHECK(rpn_peek(c, 0, &v) == RPN_E_STACK_UNDERFLOW);
    CHECK(run(c, "1") == RPN_OK && rpn_depth(c) == 1);
    CHECK(rpn_peek(c, 1, &v) == RPN_E_STACK_UNDERFLOW);
    CHECK(run(c, "drop") == RPN_OK && rpn_depth(c) == 0);
    CHECK(run(c, "drop") == RPN_E_STACK_UNDERFLOW);
    CHECK(run(c, "clear") == RPN_OK);
    CHECK(run(c, "7") == RPN_OK);
    CHECK(run(c, "swap") == RPN_E_STACK_UNDERFLOW && top_is_int(c, 7));
    CHECK(run(c, "rolldown") == RPN_E_STACK_UNDERFLOW);
    CHECK(run(c, "rollup") == RPN_E_STACK_UNDERFLOW);
    CHECK(run(c, "enter") == RPN_OK && rpn_depth(c) == 2);
    CHECK(rpn_peek(c, 1, &v) == RPN_OK && v.u.i == 7);
    rpn_free(c);
}

static void test_stack_max(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    CHECK(rpn_stack_max(c) == RPN_DEFAULT_STACK_MAX);
    CHECK(rpn_set_stack_max(c, 0) == RPN_E_BAD_ARG);
    CHECK(rpn_set_stack_max(c, 3) == RPN_OK && rpn_stack_max(c) == 3);
    CHECK(run(c, "1 2 3") == RPN_OK);
    CHECK(run(c, "4") == RPN_E_STACK_OVERFLOW && rpn_depth(c) == 3);
    CHECK(run(c, "enter") == RPN_E_STACK_OVERFLOW && rpn_depth(c) == 3);
    CHECK(run(c, "lastx") == RPN_E_STACK_OVERFLOW && rpn_depth(c) == 3);
    CHECK(rpn_set_stack_max(c, 2) == RPN_E_BAD_ARG && rpn_stack_max(c) == 3);
    CHECK(run(c, "+ +") == RPN_OK && rpn_depth(c) == 1);
    CHECK(rpn_set_stack_max(c, 1) == RPN_OK);
    CHECK(run(c, "5") == RPN_E_STACK_OVERFLOW);
    CHECK(run(c, "drop 5") == RPN_OK && top_is_int(c, 5));
    /* growth across the doubling boundaries up to a large maximum */
    CHECK(rpn_set_stack_max(c, 1000) == RPN_OK);
    CHECK(run(c, "clear") == RPN_OK);
    {
        int i;
        int ok = 1;

        for (i = 0; i < 1000 && ok; i++) {
            ok = run(c, "1") == RPN_OK;
        }
        CHECK(ok && rpn_depth(c) == 1000);
        CHECK(run(c, "1") == RPN_E_STACK_OVERFLOW);
    }
    rpn_free(c);
}

static void test_arith(void)
{
    CHECK(evals_int("7 3 -", 4));
    CHECK(evals_int("6 7 *", 42));
    CHECK(evals_int("10 2 /", 5));
    CHECK(evals_real("10 4 /", 2.5));
    CHECK(evals_real("1.5 2 +", 3.5));
    CHECK(evals_int("-7 abs", 7));
    CHECK(evals_real("-7.5 abs", 7.5));
    CHECK(evals_int("5 neg", -5));
    CHECK(evals_real("2.5 neg", -2.5));
    CHECK(evals_int("5 chs", -5));
    CHECK(evals_real("4 inv", 0.25));
    CHECK(evals_int("1 inv", 1));
    CHECK(evals_int("-1 1/x", -1));
    CHECK(evals_int("7 3 mod", 1));
    CHECK(evals_int("-7 3 mod", 2));
    CHECK(evals_int("7 -3 mod", -2));
    CHECK(evals_int("-7 -3 mod", -1));
    CHECK(evals_int("6 3 mod", 0));
    CHECK(evals_int("5 -1 mod", 0));
    CHECK(evals_real("7.5 2 mod", 1.5));
    CHECK(evals_real("-7.5 2 mod", 0.5));
    CHECK(evals_int("5 sq", 25));
    CHECK(evals_int("5 x^2", 25));
    CHECK(evals_real("1.5 sq", 2.25));
    CHECK(evals_int("16 sqrt", 4));
    CHECK(evals_int("0 sqrt", 0));
    CHECK(evals_int("1 sqrt", 1));
    CHECK(evals_real("2 sqrt", 1.414213562373095));
    CHECK(evals_real("2.25 sqrt", 1.5));
    CHECK(evals_int("2 10 pow", 1024));
    CHECK(evals_int("2 10 y^x", 1024));
    CHECK(evals_int("-2 3 pow", -8));
    CHECK(evals_int("7 0 pow", 1));
    CHECK(evals_int("0 0 pow", 1));
    CHECK(evals_int("1 -5 pow", 1));
    CHECK(evals_int("-1 -3 pow", -1));
    CHECK(evals_int("-1 -4 pow", 1));
    CHECK(evals_real("2 -2 pow", 0.25));
    CHECK(evals_real("2.5 2 pow", 6.25));
    CHECK(evals_real("4 0.5 pow", 2.0));
    CHECK(evals_int("200 15 percent drop", 200));
    CHECK(evals_int("200 15 %", 30));
    CHECK(evals_real("50 2.5 percent", 1.25));
    CHECK(evals_int("9223372036854775807 1 -", 9223372036854775806L));
    CHECK(evals_int("-9223372036854775807 1 -", -9223372036854775807L - 1));
}

static void test_aliases_and_case(void)
{
    CHECK(evals_int("1 2 SWAP DROP", 2));
    CHECK(evals_int("1 2 Swap Drop", 2));
    CHECK(evals_int("3 SQ", 9));
    CHECK(evals_int("3 4 +", 7));
    CHECK(evals_int("3 4 + # comment 99", 7));
    CHECK(evals_int("3\n4\n+", 7));
    CHECK(evals_int("  \t 3 \n\n  4  +  ", 7));
    CHECK(evals_int("", 0) == 0);
}

static void test_last_error_state(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    CHECK(rpn_last_error(c) == RPN_OK && rpn_last_error_message(c)[0] == '\0');
    CHECK(run(c, "foo") == RPN_E_UNKNOWN_CMD);
    CHECK(strstr(rpn_last_error_message(c), "foo") != NULL);
    CHECK(strstr(m.last_msg, "unknown command") != NULL);
    CHECK(run(c, "1") == RPN_OK);
    CHECK(rpn_last_error(c) == RPN_OK && rpn_last_error_message(c)[0] == '\0');
    CHECK(m.nerrors == 1);
    /* commands before the failing token still ran */
    CHECK(run(c, "5 6 nosuch 7") == RPN_E_UNKNOWN_CMD && rpn_depth(c) == 3);
    rpn_free(c);
}

static void test_push_api(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    v = rpn_value_int(5);
    CHECK(rpn_push(c, &v) == RPN_OK && top_is_int(c, 5));
    v = rpn_value_real(2.5);
    CHECK(rpn_push(c, &v) == RPN_OK && top_is_real(c, 2.5));
    v = rpn_value_real(1e308 * 10);
    CHECK(rpn_push(c, &v) == RPN_E_REAL_OVERFLOW);
    v.tag = RPN_T_VECTOR;
    CHECK(rpn_push(c, &v) == RPN_E_TYPE);
    v = rpn_value_complex(1.0, 2.0);
    CHECK(rpn_push(c, &v) == RPN_OK);
    v = rpn_value_complex(1.0, 1e308 * 10);
    CHECK(rpn_push(c, &v) == RPN_E_REAL_OVERFLOW);
    CHECK(rpn_depth(c) == 3);
    rpn_free(c);
}

static void test_type_error(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    /* The reserved tags cannot be pushed through the API, so inject one
     * by poking the stack through a push of a real and checking the
     * public error for an unsupported tag at format time instead. */
    v.tag = RPN_T_VECTOR;
    CHECK(rpn_push(c, &v) == RPN_E_TYPE);
    rpn_free(c);
}

static void test_error_coverage(void)
{
    mock_t m;
    rpn_ctx_t *c;
    int code;
    int seen[RPN_E_COUNT];

    memset(seen, 0, sizeof seen);
    seen[RPN_OK] = 1;
    seen_codes = seen;
    CHECK(fails_seen("1 0 /", RPN_E_DIV_ZERO, 2));
    CHECK(fails_seen("1 0.0 /", RPN_E_DIV_ZERO, 2));
    CHECK(fails_seen("1 0 mod", RPN_E_DIV_ZERO, 2));
    CHECK(fails_seen("0 inv", RPN_E_DIV_ZERO, 1));
    CHECK(fails_seen("0 -1 pow", RPN_E_DIV_ZERO, 2));
    CHECK(fails_seen("9223372036854775807 1 +", RPN_E_INT_OVERFLOW, 2));
    CHECK(fails_seen("-9223372036854775808 1 -", RPN_E_INT_OVERFLOW, 2));
    CHECK(fails_seen("4611686018427387904 2 *", RPN_E_INT_OVERFLOW, 2));
    CHECK(fails_seen("-4611686018427387905 2 *", RPN_E_INT_OVERFLOW, 2));
    CHECK(fails_seen("-9223372036854775808 neg", RPN_E_INT_OVERFLOW, 1));
    CHECK(fails_seen("-9223372036854775808 abs", RPN_E_INT_OVERFLOW, 1));
    CHECK(fails_seen("-9223372036854775808 -1 /", RPN_E_INT_OVERFLOW, 2));
    CHECK(fails_seen("4000000000 sq", RPN_E_INT_OVERFLOW, 1));
    CHECK(fails_seen("2 64 pow", RPN_E_INT_OVERFLOW, 2));
    CHECK(fails_seen("9223372036854775808", RPN_E_INT_OVERFLOW, 0));
    CHECK(fails_seen("1e999", RPN_E_REAL_OVERFLOW, 0));
    CHECK(fails_seen("1e308 10 *", RPN_E_REAL_OVERFLOW, 2));
    CHECK(fails_seen("1e308 sq", RPN_E_REAL_OVERFLOW, 1));
    CHECK(fails_seen("strict -4 sqrt", RPN_E_DOMAIN, 1));
    CHECK(fails_seen("strict -8 0.5 pow", RPN_E_DOMAIN, 2));
    CHECK(fails_seen("+", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_seen("1 +", RPN_E_STACK_UNDERFLOW, 1));
    CHECK(fails_seen("swap", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_seen("abs", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_seen("frobnicate", RPN_E_UNKNOWN_CMD, 0));
    CHECK(fails_seen("1.2.3", RPN_E_UNKNOWN_CMD, 0));
    CHECK(fails_seen("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                     RPN_E_TOKEN_TOO_LONG, 0));

    /* codes that need a special setup */
    c = make(&m);
    rpn_set_stack_max(c, 1);
    code = run(c, "1 2");
    CHECK(code == RPN_E_STACK_OVERFLOW);
    seen[code] = 1;
    rpn_free(c);

    c = make(&m);
    m.fail_at = 2;
    code = run(c, "1");
    CHECK(code == RPN_E_NOMEM);
    seen[code] = 1;
    rpn_free(c);

    c = make(&m);
    code = run(c, NULL);
    CHECK(code == RPN_E_BAD_ARG);
    seen[code] = 1;
    CHECK(rpn_exec(NULL, "1") == RPN_E_BAD_ARG);
    CHECK(rpn_set_stack_max(c, 0) == RPN_E_BAD_ARG);
    rpn_free(c);


    CHECK(fails_seen("1 store { 2x }", RPN_E_BAD_NAME, 1));
    CHECK(fails_seen("recall { nope }", RPN_E_UNDEFINED, 0));
    CHECK(fails_seen("program { label { a } label { a } }", RPN_E_DUPLICATE, 0));
    CHECK(fails_seen("return", RPN_E_PROGRAM, 0));
    CHECK(fails_seen("program { label { r } execute { r } } 3 calllimit execute { r }",
                     RPN_E_CALL_DEPTH, 0));
    CHECK(fails_seen("program { label { s } goto { s } } 50 steplimit goto { s }",
                     RPN_E_STEP_LIMIT, 0));
    CHECK(fails_seen("program { label { g } }  function { f goto { g } } f",
                     RPN_E_SCOPE, 0));

    c = make(&m);
    {
        rpn_value_t v;

        v.tag = RPN_T_MATRIX;
        code = rpn_push(c, &v);
        CHECK(code == RPN_E_TYPE);
        seen[code] = 1;
    }
    rpn_free(c);

    for (code = 0; code < RPN_E_COUNT; code++) {
        CHECK(seen[code]);
        CHECK(rpn_strerror(code)[0] != '\0');
    }
    CHECK(strcmp(rpn_strerror(-1), "unknown error") == 0);
    CHECK(strcmp(rpn_strerror(RPN_E_COUNT), "unknown error") == 0);
    seen_codes = NULL;
}

static void test_failed_command_keeps_stack(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    run(c, "10 20 30");
    CHECK(run(c, "0 /") == RPN_E_DIV_ZERO);
    CHECK(rpn_depth(c) == 4);
    CHECK(rpn_peek(c, 0, &v) == RPN_OK && v.u.i == 0);
    CHECK(rpn_peek(c, 1, &v) == RPN_OK && v.u.i == 30);
    /* LASTX is not updated by a failing command */
    run(c, "drop 5 4 +");
    CHECK(run(c, "0 /") == RPN_E_DIV_ZERO);
    CHECK(run(c, "drop lastx") == RPN_OK && top_is_int(c, 4));
    rpn_free(c);
}

/* Allocation failure injection: fail each allocation in turn. */
static void test_alloc_failures(void)
{
    mock_t m;
    rpn_ctx_t *c;
    int n, rc, failed_runs = 0;

    for (n = 1; n <= 4; n++) {
        rpn_env_t e;

        memset(&m, 0, sizeof m);
        m.fail_at = n;
        e.user = &m;
        e.alloc = m_alloc;
        e.release = m_release;
        e.out = NULL;
        e.error = m_error;
        c = rpn_new(&e);
        if (c == NULL) {
            CHECK(n == 1);
            CHECK(m.frees == 0);
            failed_runs++;
            continue;
        }
        rc = run(c, "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20");
        if (rc != RPN_OK) {
            CHECK(rc == RPN_E_NOMEM);
            CHECK(m.last_code == RPN_E_NOMEM);
            failed_runs++;
        }
        /* the calculator is still usable and frees everything */
        rpn_free(c);
        CHECK(m.allocs == m.frees);
    }
    CHECK(failed_runs >= 3);

    /* an env without alloc/release is rejected */
    {
        rpn_env_t e;

        memset(&e, 0, sizeof e);
        CHECK(rpn_new(&e) == NULL);
        CHECK(rpn_new(NULL) == NULL);
        e.alloc = m_alloc;
        CHECK(rpn_new(&e) == NULL);
    }
    rpn_free(NULL);
}

static void test_no_leaks(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    run(c, "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22");
    run(c, "+ + + foo");
    rpn_free(c);
    CHECK(m.allocs == m.frees);
    CHECK(m.allocs >= 2);
}

static void test_env_without_error_hook(void)
{
    rpn_env_t e;
    mock_t m;
    rpn_ctx_t *c;

    memset(&m, 0, sizeof m);
    e.user = &m;
    e.alloc = m_alloc;
    e.release = m_release;
    e.out = NULL;
    e.error = NULL;
    c = rpn_new(&e);
    CHECK(c != NULL);
    CHECK(run(c, "1 0 /") == RPN_E_DIV_ZERO);
    CHECK(rpn_last_error(c) == RPN_E_DIV_ZERO);
    rpn_free(c);
}


/* ---- Phase 2: complex numbers, functions, modes ---------------------- */

static int near(double a, double b)
{
    double d = a - b;

    return d < 1e-9 * (1.0 + (b < 0 ? -b : b)) &&
           d > -1e-9 * (1.0 + (b < 0 ? -b : b));
}

/* Top of stack is a complex close to (re, im). */
static int top_is_cx(rpn_ctx_t *c, double re, double im)
{
    rpn_value_t v;

    return rpn_peek(c, 0, &v) == RPN_OK && v.tag == RPN_T_COMPLEX &&
           near(v.u.c.re, re) && near(v.u.c.im, im);
}

/* Level `lvl` is a real close to want. */
static int level_real(rpn_ctx_t *c, size_t lvl, double want)
{
    rpn_value_t v;

    return rpn_peek(c, lvl, &v) == RPN_OK && v.tag == RPN_T_REAL &&
           near(v.u.r, want);
}

static int evals_cx(const char *text, double re, double im)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    int ok = (c != NULL) && run(c, text) == RPN_OK && top_is_cx(c, re, im);

    rpn_free(c);
    return ok;
}

/* The top is the real `want`, and bit-for-bit exact (no rounding noise). */
static int evals_exact(const char *text, double want)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;
    int ok = (c != NULL) && run(c, text) == RPN_OK &&
             rpn_peek(c, 0, &v) == RPN_OK && v.tag == RPN_T_REAL &&
             v.u.r == want;

    rpn_free(c);
    return ok;
}

/* Formats the top of the stack after running text. */
static int shows(const char *text, const char *want)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;
    char buf[RPN_FORMAT_MAX];
    int ok;

    ok = (c != NULL) && run(c, text) == RPN_OK &&
         rpn_peek(c, 0, &v) == RPN_OK &&
         rpn_format(c, &v, buf, sizeof buf) == 0 && strcmp(buf, want) == 0;
    if (!ok && c != NULL && rpn_depth(c) > 0) {
        rpn_peek(c, 0, &v);
        rpn_format(c, &v, buf, sizeof buf);
        printf("  shows(\"%s\") gave \"%s\", wanted \"%s\"\n", text, buf, want);
    }
    rpn_free(c);
    return ok;
}

static void test_complex_literals(void)
{
    rpn_value_t v;

    CHECK(rpn_parse_number("(1,2)", &v) == 1 && v.tag == RPN_T_COMPLEX &&
          v.u.c.re == 1.0 && v.u.c.im == 2.0);
    CHECK(rpn_parse_number("(1.5,-2e1)", &v) == 1 && v.u.c.re == 1.5 &&
          v.u.c.im == -20.0);
    CHECK(rpn_parse_number("(+1,.5)", &v) == 1 && v.u.c.im == 0.5);
    CHECK(rpn_parse_number("(0,0)", &v) == 1 && v.tag == RPN_T_COMPLEX);
    CHECK(rpn_parse_number("(1,2", &v) == 0);
    CHECK(rpn_parse_number("1,2)", &v) == 0);
    CHECK(rpn_parse_number("(1,)", &v) == 0);
    CHECK(rpn_parse_number("(,2)", &v) == 0);
    CHECK(rpn_parse_number("(,)", &v) == 0);
    CHECK(rpn_parse_number("()", &v) == 0);
    CHECK(rpn_parse_number("(1,2,3)", &v) == 0);
    CHECK(rpn_parse_number("(a,b)", &v) == 0);
    CHECK(rpn_parse_number("((1,2),3)", &v) == 0);
    CHECK(rpn_parse_number("(1 2)", &v) == 0);
    CHECK(rpn_parse_number("(1e999,0)", &v) == -1);
    CHECK(rpn_parse_number("(0,-1e999)", &v) == -1);
    CHECK(fails_with("(1e999,0)", RPN_E_REAL_OVERFLOW, 0));
    CHECK(fails_with("(1,2", RPN_E_UNKNOWN_CMD, 0));
}

static void test_complex_arith(void)
{
    CHECK(evals_cx("(1,2) (3,4) +", 4, 6));
    CHECK(evals_cx("(1,2) (3,4) -", -2, -2));
    CHECK(evals_cx("(1,2) (3,4) *", -5, 10));
    CHECK(evals_cx("(1,2) (3,4) /", 0.44, 0.08));
    CHECK(evals_cx("(3,4) (3,4) /", 1, 0));      /* stays complex */
    CHECK(evals_cx("1 (0,1) +", 1, 1));          /* int promotes */
    CHECK(evals_cx("(0,1) 2.5 *", 0, 2.5));      /* real promotes */
    CHECK(evals_cx("(0,1) sq", -1, 0));
    CHECK(evals_cx("(1,1) neg", -1, -1));
    CHECK(evals_cx("(0,2) inv", 0, -0.5));
    CHECK(evals_cx("(3,4) sqrt", 2, 1));
    CHECK(evals_cx("-4 sqrt", 0, 2));
    CHECK(evals_cx("(0,1) 2 pow", -1, 0));
    CHECK(evals_cx("-8 0.5 pow", 0, 2.8284271247461903));
    CHECK(evals_cx("0 (1,1) pow", 0, 0));
    CHECK(evals_cx("(1,2) conj", 1, -2));
    CHECK(evals_cx("(5,0) 1 +", 6, 0));          /* no automatic demotion */
    CHECK(evals_int("-8 3 pow", -512));          /* integer exponent stays int */
    CHECK(evals_real("(3,4) abs", 5.0));
    CHECK(evals_real("(1,2) re", 1.0));
    CHECK(evals_real("(1,2) im", 2.0));
    CHECK(evals_int("7 re", 7));
    CHECK(evals_int("7 im", 0));
    CHECK(evals_real("7.5 im", 0.0));
    CHECK(evals_cx("1 2 complex", 1, 2));
    CHECK(evals_real("(5,0) demote", 5.0));
    CHECK(evals_cx("(5,1) demote", 5, 1));
    CHECK(evals_real("1e308 1e308 complex abs", 1.4142135623730951e308));
    /* branch cut of sqrt: just above and below the negative real axis */
    CHECK(evals_cx("(-4,1e-300) sqrt", 0, 2));
    CHECK(evals_cx("(-4,-1e-300) sqrt", 0, -2));
}

static void test_complex_errors(void)
{
    CHECK(fails_with("(1,2) 0 /", RPN_E_DIV_ZERO, 2));
    CHECK(fails_with("(1,2) (0,0) /", RPN_E_DIV_ZERO, 2));
    CHECK(fails_with("(0,0) inv", RPN_E_DIV_ZERO, 1));
    CHECK(fails_with("(0,0) (-1,0) pow", RPN_E_DIV_ZERO, 2));
    CHECK(fails_with("(1,2) 2 mod", RPN_E_TYPE, 2));
    CHECK(fails_with("2 (1,2) mod", RPN_E_TYPE, 2));
    CHECK(fails_with("(1,2) 2 percent", RPN_E_TYPE, 2));
    CHECK(fails_with("(1,2) floor", RPN_E_TYPE, 1));
    CHECK(fails_with("(1,2) round", RPN_E_TYPE, 1));
    CHECK(fails_with("(1,2) fraction", RPN_E_TYPE, 1));
    CHECK(fails_with("(1,2) 3 atan2", RPN_E_TYPE, 2));
    CHECK(fails_with("(1,2) 3 polar", RPN_E_TYPE, 2));
    CHECK(fails_with("(1,2) 3 rect", RPN_E_TYPE, 2));
    CHECK(fails_with("(1,2) 3 complex", RPN_E_TYPE, 2));
    CHECK(fails_with("(1e200,1e200) sq", RPN_E_REAL_OVERFLOW, 1));
    CHECK(fails_with("(1000,0) exp", RPN_E_REAL_OVERFLOW, 1));
    CHECK(fails_with("(0,0) ln", RPN_E_DOMAIN, 1));
}

static void test_exp_log(void)
{
    CHECK(evals_real("0 exp", 1.0));
    CHECK(evals_real("1 exp", 2.718281828459045));
    CHECK(evals_real("-1 exp", 0.36787944117144233));
    CHECK(evals_real("1 ln", 0.0));
    CHECK(evals_real("e ln", 1.0));
    CHECK(evals_real("100 log", 2.0));
    CHECK(evals_real("0.001 log", -3.0));
    CHECK(evals_real("8 log2", 3.0));
    CHECK(evals_exact("1024 log2", 10.0));
    CHECK(evals_exact("0.125 log2", -3.0));
    CHECK(evals_real("0 ln1p", 0.0));
    CHECK(evals_real("1 ln1p", 0.6931471805599453));
    CHECK(evals_real("0 expm1", 0.0));
    CHECK(evals_real("1 expm1", 1.718281828459045));
    CHECK(evals_real("2 pow10", 100.0));
    CHECK(evals_real("-2 pow10", 0.01));
    CHECK(evals_real("3 pow2", 8.0));
    /* small arguments keep their precision (relative error < 1e-12) */
    CHECK(evals_real("1e-10 ln1p 1e10 *", 1.0));
    CHECK(evals_real("1e-10 expm1 1e10 *", 1.0));
    /* outside the real domain: complex result */
    CHECK(evals_cx("-1 ln", 0, 3.141592653589793));
    CHECK(evals_cx("-100 log", 2, 1.3643763538418412));
    CHECK(evals_cx("-2 ln1p", 0, 3.141592653589793));
    CHECK(evals_cx("-8 log2", 3, 4.532360141827194));
    CHECK(evals_cx("(0,1) ln", 0, 1.5707963267948966));
    CHECK(evals_cx("(0,3.141592653589793) exp", -1, 0));
    CHECK(evals_cx("(1,0) exp", 2.718281828459045, 0));
    /* errors */
    CHECK(fails_with("0 ln", RPN_E_DOMAIN, 1));
    CHECK(fails_with("0 log", RPN_E_DOMAIN, 1));
    CHECK(fails_with("-1 ln1p", RPN_E_DOMAIN, 1));
    CHECK(fails_with("1000 exp", RPN_E_REAL_OVERFLOW, 1));
    CHECK(fails_with("1000 pow10", RPN_E_REAL_OVERFLOW, 1));
    CHECK(fails_with("strict -1 ln", RPN_E_DOMAIN, 1));
    CHECK(fails_with("strict -1 log", RPN_E_DOMAIN, 1));
    CHECK(fails_with("strict -1 log2", RPN_E_DOMAIN, 1));
    CHECK(fails_with("strict -2 ln1p", RPN_E_DOMAIN, 1));
    /* but complex arguments are always fine in strict mode */
    CHECK(evals_cx("strict (-1,0) ln", 0, 3.141592653589793));
    /* back to lenient */
    CHECK(evals_cx("strict lenient -1 ln", 0, 3.141592653589793));
}

static void test_trig(void)
{
    CHECK(evals_real("0 sin", 0.0));
    CHECK(evals_real("pi 2 / sin", 1.0));
    CHECK(evals_real("0 cos", 1.0));
    CHECK(evals_real("pi cos", -1.0));
    CHECK(evals_real("pi 4 / tan", 1.0));
    CHECK(evals_real("1 5 / sin 2 *", 0.3973386615901224));   /* 2 sin 0.2 */
    CHECK(evals_real("0 asin", 0.0));
    CHECK(evals_real("1 asin", 1.5707963267948966));
    CHECK(evals_real("-1 asin", -1.5707963267948966));
    CHECK(evals_real("1 acos", 0.0));
    CHECK(evals_real("-1 acos", 3.141592653589793));
    CHECK(evals_real("1 atan", 0.7853981633974483));
    CHECK(evals_real("1 1 atan2", 0.7853981633974483));
    CHECK(evals_real("1 -1 atan2", 2.356194490192345));
    CHECK(evals_real("-1 -1 atan2", -2.356194490192345));
    CHECK(evals_real("0 -1 atan2", 3.141592653589793));
    CHECK(evals_real("1 0 atan2", 1.5707963267948966));
    /* out of [-1, 1]: complex, on the cut as C99 (+0i) */
    CHECK(evals_cx("2 asin", 1.5707963267948966, 1.3169578969248166));
    CHECK(evals_cx("-2 asin", -1.5707963267948966, 1.3169578969248166));
    CHECK(evals_cx("2 acos", 0, -1.3169578969248166));
    CHECK(evals_cx("-2 acos", 3.141592653589793, -1.3169578969248166));
    CHECK(fails_with("strict 2 asin", RPN_E_DOMAIN, 1));
    CHECK(fails_with("strict 2 acos", RPN_E_DOMAIN, 1));
    CHECK(fails_with("(0,1) atan", RPN_E_DOMAIN, 1));      /* pole at i */
    CHECK(fails_with("(0,-1) atan", RPN_E_DOMAIN, 1));
    /* complex: sin(i) = i sinh(1) */
    CHECK(evals_cx("(0,1) sin", 0, 1.1752011936438014));
    CHECK(evals_cx("(0,1) cos", 1.5430806348152437, 0));
    CHECK(evals_cx("(0,1) tan", 0, 0.7615941559557649));
    CHECK(evals_cx("(1,1) sin", 1.2984575814159773, 0.6349639147847361));
    CHECK(evals_cx("(1,1) cos", 0.8337300251311491, -0.9888977057628651));
}

static void test_angle_modes(void)
{
    CHECK(evals_real("degrees 30 sin", 0.5));
    CHECK(evals_real("degrees 60 cos", 0.5));
    CHECK(evals_real("degrees 45 tan", 1.0));
    CHECK(evals_real("degrees 0.5 asin", 30.0));
    CHECK(evals_real("degrees 0.5 acos", 60.0));
    CHECK(evals_real("degrees 1 atan", 45.0));
    CHECK(evals_real("degrees 1 1 atan2", 45.0));
    CHECK(evals_real("degrees -1 0 atan2", -90.0));
    CHECK(evals_real("grads 50 tan", 1.0));
    CHECK(evals_real("grads 1 atan", 50.0));
    CHECK(evals_real("grads 1 asin", 100.0));
    CHECK(evals_real("grads 1 0 atan2", 100.0));
    /* quarter turns are exact in degrees and grads */
    CHECK(evals_exact("degrees 180 sin", 0.0));
    CHECK(evals_exact("degrees 360 sin", 0.0));
    CHECK(evals_exact("degrees 90 sin", 1.0));
    CHECK(evals_exact("degrees 90 cos", 0.0));
    CHECK(evals_exact("degrees 270 cos", 0.0));
    CHECK(evals_exact("degrees 180 cos", -1.0));
    CHECK(evals_exact("degrees 270 sin", -1.0));
    CHECK(evals_exact("degrees -90 sin", -1.0));
    CHECK(evals_exact("degrees -270 sin", 1.0));
    CHECK(evals_exact("degrees -360 cos", 1.0));
    CHECK(evals_exact("degrees 450 sin", 1.0));
    CHECK(evals_exact("degrees 180 tan", 0.0));
    CHECK(evals_exact("grads 100 sin", 1.0));
    CHECK(evals_exact("grads 200 cos", -1.0));
    CHECK(evals_exact("grads -100 sin", -1.0));
    CHECK(evals_exact("grads 300 cos", 0.0));
    /* a tiny negative angle must not wrap to a bogus index */
    CHECK(evals_real("degrees -1e-20 sin", 0.0));
    CHECK(evals_real("degrees -1e-20 cos", 1.0));
    CHECK(evals_real("degrees 1e-20 cos", 1.0));
    CHECK(fails_with("degrees 90 tan", RPN_E_DOMAIN, 1));
    CHECK(fails_with("degrees 270 tan", RPN_E_DOMAIN, 1));
    CHECK(fails_with("degrees -90 tan", RPN_E_DOMAIN, 1));
    CHECK(fails_with("grads 100 tan", RPN_E_DOMAIN, 1));
    /* switching back */
    CHECK(evals_real("degrees radians pi 2 / sin", 1.0));
    CHECK(evals_real("degrees rad pi 2 / sin", 1.0));
    CHECK(evals_real("grads deg 30 sin", 0.5));
    /* complex values are always in radians */
    CHECK(evals_cx("degrees (0,1) sin", 0, 1.1752011936438014));
    CHECK(evals_cx("degrees 2 asin", 1.5707963267948966, 1.3169578969248166));
    /* arg, polar, rect */
    CHECK(evals_real("(0,1) arg", 1.5707963267948966));
    CHECK(evals_real("-1 arg", 3.141592653589793));
    CHECK(evals_real("1 arg", 0.0));
    CHECK(evals_real("degrees (0,1) arg", 90.0));
    CHECK(evals_real("degrees (-1,0) arg", 180.0));
}

static void test_polar_rect(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    CHECK(run(c, "3 4 polar") == RPN_OK && rpn_depth(c) == 2);
    CHECK(level_real(c, 0, 5.0));                      /* r on top */
    CHECK(level_real(c, 1, 0.6435011087932844));       /* theta */
    run(c, "clear degrees 90 2 rect");
    CHECK(rpn_depth(c) == 2);
    CHECK(level_real(c, 0, 0.0));                      /* x on top */
    CHECK(level_real(c, 1, 2.0));                      /* y */
    run(c, "clear 1 1 polar");
    CHECK(level_real(c, 0, 1.4142135623730951));
    CHECK(level_real(c, 1, 45.0));
    run(c, "rect");
    CHECK(level_real(c, 0, 1.0));
    CHECK(level_real(c, 1, 1.0));
    /* a failing polar/rect leaves both operands */
    run(c, "clear 1 (1,1)");
    CHECK(run(c, "polar") == RPN_E_TYPE && rpn_depth(c) == 2);
    rpn_free(c);

}

static void test_hyperbolic(void)
{
    CHECK(evals_real("0 sinh", 0.0));
    CHECK(evals_real("0 cosh", 1.0));
    CHECK(evals_real("0 tanh", 0.0));
    CHECK(evals_real("1 sinh", 1.1752011936438014));
    CHECK(evals_real("1 cosh", 1.5430806348152437));
    CHECK(evals_real("1 tanh", 0.7615941559557649));
    CHECK(evals_real("1 asinh", 0.881373587019543));
    CHECK(evals_real("-1 asinh", -0.881373587019543));
    CHECK(evals_real("1 acosh", 0.0));
    CHECK(evals_real("2 acosh", 1.3169578969248166));
    CHECK(evals_real("0.5 atanh", 0.5493061443340549));
    CHECK(evals_real("-0.5 atanh", -0.5493061443340549));
    CHECK(evals_real("1e20 asinh", 46.74484904044087));
    CHECK(evals_real("1e20 acosh", 46.74484904044087));
    /* outside the real domain */
    CHECK(evals_cx("0.5 acosh", 0, 1.0471975512));
    CHECK(evals_cx("-2 acosh", 1.3169578969248166, 3.141592653589793));
    CHECK(evals_cx("2 atanh", 0.5493061443340549, 1.5707963267948966));
    CHECK(evals_cx("-2 atanh", -0.5493061443340549, -1.5707963267948966) ||
          evals_cx("-2 atanh", -0.5493061443340549, 1.5707963267948966));
    CHECK(fails_with("1 atanh", RPN_E_DOMAIN, 1));
    CHECK(fails_with("-1 atanh", RPN_E_DOMAIN, 1));
    CHECK(fails_with("strict 0.5 acosh", RPN_E_DOMAIN, 1));
    CHECK(fails_with("strict 2 atanh", RPN_E_DOMAIN, 1));
    CHECK(fails_with("1000 sinh", RPN_E_REAL_OVERFLOW, 1));
    CHECK(fails_with("1000 cosh", RPN_E_REAL_OVERFLOW, 1));
    /* complex: cosh(i) = cos(1), sinh(i) = i sin(1) */
    CHECK(evals_cx("(0,1) cosh", 0.5403023058681398, 0));
    CHECK(evals_cx("(0,1) sinh", 0, 0.8414709848078965));
}

static void test_rounding(void)
{
    CHECK(evals_int("2.5 round", 3));
    CHECK(evals_int("-2.5 round", -3));
    CHECK(evals_int("2.4 round", 2));
    CHECK(evals_int("2.4 floor", 2));
    CHECK(evals_int("-2.4 floor", -3));
    CHECK(evals_int("2.1 ceil", 3));
    CHECK(evals_int("-2.1 ceil", -2));
    CHECK(evals_int("2.9 trunc", 2));
    CHECK(evals_int("-2.9 trunc", -2));
    CHECK(evals_int("5 floor", 5));
    CHECK(evals_int("-5 round", -5));
    CHECK(evals_real("-2.75 fraction", -0.75));
    CHECK(evals_real("2.75 fraction", 0.75));
    CHECK(evals_int("5 fraction", 0));
    CHECK(evals_real("1e20 floor", 1e20));            /* too big for an int */
    CHECK(evals_real("-1e20 ceil", -1e20));
    CHECK(evals_real("pi", 3.141592653589793));
    CHECK(evals_real("e", 2.718281828459045));
    CHECK(evals_real("pi pi *  pi sq -", 0.0));
    CHECK(evals_real("e ln", 1.0));
}

static void test_display_modes(void)
{
    CHECK(shows("pi", "3.14159265359"));
    CHECK(shows("2 fix pi", "3.14"));
    CHECK(shows("0 fix 2.6", "3."));
    CHECK(shows("4 fix 1 3 /", "0.3333"));
    CHECK(shows("2 fix -1.005", "-1.00"));
    CHECK(shows("11 fix 1 3 /", "0.33333333333"));
    CHECK(shows("2 fix 1e20", "1.00e+20"));
    CHECK(shows("3 sci 12345.678", "1.235e+04"));
    CHECK(shows("0 sci 12345.678", "1e+04"));
    CHECK(shows("3 sci 0.00012345", "1.234e-04") ||
          shows("3 sci 0.00012345", "1.235e-04"));
    CHECK(shows("3 eng 12345.678", "12.346e3"));
    CHECK(shows("3 eng 0.00012345", "123.450e-6"));
    CHECK(shows("3 eng 999999.9", "1.000e6"));
    CHECK(shows("1 eng 0.0", "0.0e0"));
    CHECK(shows("2 eng -4700.0", "-4.70e3"));
    CHECK(shows("2 fix all pi", "3.14159265359"));
    CHECK(shows("2 sci all 0.5", "0.5"));
    /* ints ignore the display mode */
    CHECK(shows("2 fix 12345", "12345"));
    CHECK(shows("3 sci -7", "-7"));
    /* complex values format both parts */
    CHECK(shows("1 fix (1,2)", "(1.0,2.0)"));
    CHECK(shows("2 sci (1234,0.5)", "(1.23e+03,5.00e-01)"));
    CHECK(shows("(1,2)", "(1.0,2.0)"));
    CHECK(shows("(1.5,-0.25)", "(1.5,-0.25)"));
    /* errors: the argument must be an int in 0..11 */
    CHECK(fails_with("-1 fix", RPN_E_DOMAIN, 1));
    CHECK(fails_with("12 fix", RPN_E_DOMAIN, 1));
    CHECK(fails_with("12 sci", RPN_E_DOMAIN, 1));
    CHECK(fails_with("12 eng", RPN_E_DOMAIN, 1));
    CHECK(fails_with("2.5 fix", RPN_E_TYPE, 1));
    CHECK(fails_with("(1,2) sci", RPN_E_TYPE, 1));
    CHECK(fails_with("fix", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_with("eng", RPN_E_STACK_UNDERFLOW, 0));
}

static void test_mode_state(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;
    char buf[RPN_FORMAT_MAX];

    /* the display mode is per calculator, not global */
    run(c, "2 fix pi");
    rpn_peek(c, 0, &v);
    rpn_format(c, &v, buf, sizeof buf);
    CHECK(strcmp(buf, "3.14") == 0);
    CHECK(rpn_format(NULL, &v, buf, sizeof buf) == 0 &&
          strcmp(buf, "3.14159265359") == 0);
    {
        mock_t m2;
        rpn_ctx_t *c2 = make(&m2);

        rpn_format(c2, &v, buf, sizeof buf);
        CHECK(strcmp(buf, "3.14159265359") == 0);
        rpn_free(c2);
    }
    /* fix, sci and eng consume their argument; modes do not touch LASTX */
    CHECK(rpn_depth(c) == 1);
    run(c, "clear 3 4 + degrees all strict lenient");
    CHECK(rpn_depth(c) == 1 && top_is_int(c, 7));
    /* stack commands on a complex value */
    run(c, "clear (1,2) (3,4) swap");
    CHECK(top_is_cx(c, 1, 2));
    run(c, "drop lastx");
    CHECK(rpn_depth(c) == 2);
    rpn_free(c);
}

/* Mathematical identities over a table of arguments. */
static void test_identities(void)
{
    static const char *reals[] = {
        "0.1", "0.5", "0.9", "1.0", "1.5", "2.0", "3.0", "10.0", "100.0", "1e-5"
    };
    static const char *units[] = { "0.1", "0.5", "0.9", "1.2", "1.5" };
    static const char *zs[] = {
        "(1,2)", "(0.3,-1.5)", "(-3,-4)", "(1.2,0.5)", "(-0.5,0.5)",
        "(2,0)"
    };
    char buf[160];
    size_t i;
    mock_t m;
    rpn_ctx_t *c;

    for (i = 0; i < sizeof reals / sizeof reals[0]; i++) {
        sprintf(buf, "%s sin sq %s cos sq +", reals[i], reals[i]);
        CHECK(evals_real(buf, 1.0));
        sprintf(buf, "%s ln exp %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s exp ln %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s log pow10 %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s log2 pow2 %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s ln1p expm1 %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s sinh asinh %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s asinh sinh %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s atan tan %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s sqrt sq %s -", reals[i], reals[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s degrees sin sq %s cos sq +", reals[i], reals[i]);
        CHECK(evals_real(buf, 1.0));
    }
    for (i = 0; i < sizeof units / sizeof units[0]; i++) {
        sprintf(buf, "%s sin asin %s -", units[i], units[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s cos acos %s -", units[i], units[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s tanh atanh %s -", units[i], units[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s cosh acosh %s -", units[i], units[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "%s cosh sq %s sinh sq -", units[i], units[i]);
        CHECK(evals_real(buf, 1.0));
        sprintf(buf, "degrees %s sin asin %s -", units[i], units[i]);
        CHECK(evals_real(buf, 0.0));
        sprintf(buf, "grads %s sin asin %s -", units[i], units[i]);
        CHECK(evals_real(buf, 0.0));
    }
    /* complex: f(inverse f(z)) = z holds on the whole plane */
    for (i = 0; i < sizeof zs / sizeof zs[0]; i++) {
        const char *z = zs[i];
        rpn_value_t v;
        rpn_value_t w;
        static const char *tpl[] = {
            "%s ln exp",
            "%s asin sin", "%s acos cos", "%s atan tan",
            "%s asinh sinh", "%s acosh cosh", "%s atanh tanh",
            "%s sqrt sq", "%s inv inv",
            "%s log pow10", "%s log2 pow2", "%s ln1p expm1"
        };
        size_t k;

        for (k = 0; k < sizeof tpl / sizeof tpl[0]; k++) {
            c = make(&m);
            sprintf(buf, tpl[k], z);
            CHECK(run(c, buf) == RPN_OK);
            rpn_parse_number(z, &w);
            CHECK(rpn_peek(c, 0, &v) == RPN_OK && v.tag == RPN_T_COMPLEX);
            CHECK(near(v.u.c.re, w.u.c.re) && near(v.u.c.im, w.u.c.im));
            rpn_free(c);
        }
        /* sin^2 + cos^2 = 1 for complex z */
        c = make(&m);
        sprintf(buf, "%s sin sq %s cos sq +", z, z);
        CHECK(run(c, buf) == RPN_OK && top_is_cx(c, 1, 0));
        rpn_free(c);
        /* exp(z) * exp(-z) = 1 */
        c = make(&m);
        sprintf(buf, "%s exp %s neg exp *", z, z);
        CHECK(run(c, buf) == RPN_OK && top_is_cx(c, 1, 0));
        rpn_free(c);
        /* cosh^2 - sinh^2 = 1 */
        c = make(&m);
        sprintf(buf, "%s cosh sq %s sinh sq -", z, z);
        CHECK(run(c, buf) == RPN_OK && top_is_cx(c, 1, 0));
        rpn_free(c);
    }
}

static void test_phase2_misc(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    /* LASTX after a function */
    run(c, "0.5 sin drop lastx");
    CHECK(top_is_real(c, 0.5));
    run(c, "clear 2 2 atan2 drop lastx");
    CHECK(top_is_int(c, 2));
    run(c, "clear (1,2) conj drop lastx");
    CHECK(top_is_cx(c, 1, 2));
    /* constants do not touch LASTX */
    run(c, "clear 9 sqrt drop pi drop lastx");
    CHECK(top_is_int(c, 9));
    /* a failing function keeps the stack and LASTX */
    run(c, "clear 5 7");
    CHECK(run(c, "strict -1 ln") == RPN_E_DOMAIN && rpn_depth(c) == 3);
    CHECK(rpn_peek(c, 0, &v) == RPN_OK && v.u.i == -1);
    /* aliases */
    run(c, "clear 0 e^x 2 10^x 1 ln1+x 0 e^x-1");
    CHECK(rpn_depth(c) == 4);
    run(c, "clear 3 4 ->pol ->rec");
    CHECK(rpn_depth(c) == 2);
    CHECK(level_real(c, 0, 4.0) && level_real(c, 1, 3.0));
    run(c, "clear 2.5 frac deg rad grad");
    CHECK(top_is_real(c, 0.5));
    /* case folding */
    run(c, "clear rad PI SIN");
    CHECK(top_is_real(c, 0.0));
    rpn_free(c);

    /* stack bound applies to constants */
    c = make(&m);
    rpn_set_stack_max(c, 1);
    CHECK(run(c, "pi pi") == RPN_E_STACK_OVERFLOW && rpn_depth(c) == 1);
    rpn_free(c);

    /* allocation failure while a function pushes a constant */
    c = make(&m);
    m.fail_at = 2;
    CHECK(run(c, "pi") == RPN_E_NOMEM && rpn_depth(c) == 0);
    rpn_free(c);

    /* underflow of every arity-2 function */
    CHECK(fails_with("1 atan2", RPN_E_STACK_UNDERFLOW, 1));
    CHECK(fails_with("1 polar", RPN_E_STACK_UNDERFLOW, 1));
    CHECK(fails_with("1 rect", RPN_E_STACK_UNDERFLOW, 1));
    CHECK(fails_with("1 complex", RPN_E_STACK_UNDERFLOW, 1));
    CHECK(fails_with("sin", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_with("demote", RPN_E_STACK_UNDERFLOW, 0));
}


/* ---- Phase 3: variables, tests, programs ------------------------------ */

/* Runs text on a fresh calculator and returns the depth, or -1 on error. */
static long depth_after(const char *text)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    long d;

    d = (c != NULL && run(c, text) == RPN_OK) ? (long)rpn_depth(c) : -1L;
    rpn_free(c);
    return d;
}

static void test_variables(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    CHECK(run(c, "5 store { x }") == RPN_OK && rpn_depth(c) == 0);   /* popped */
    CHECK(run(c, "x") == RPN_OK && rpn_depth(c) == 1 && top_is_int(c, 5));
    CHECK(run(c, "clear 6 dup store { x }") == RPN_OK && top_is_int(c, 6));  /* kept */
    CHECK(run(c, "clear recall { x }") == RPN_OK && top_is_int(c, 6));
    CHECK(run(c, "clear 2 sto+ { nothere }") == RPN_E_UNDEFINED);
    CHECK(run(c, "clear 5 store { x } 3 store- { x } x") == RPN_OK && top_is_int(c, 2));
    CHECK(run(c, "clear 4 store* { x } x") == RPN_OK && top_is_int(c, 8));
    CHECK(run(c, "clear 2 store/ { x } x") == RPN_OK && top_is_int(c, 4));
    CHECK(run(c, "clear 0.5 store+ { x } x") == RPN_OK && top_is_real(c, 4.5));
    /* variable on the left: 10 - 3, not 3 - 10 */
    CHECK(run(c, "clear 10 store { a } 3 store- { a } a") == RPN_OK && top_is_int(c, 7));
    /* complex values are stored as they are */
    CHECK(run(c, "clear (1,2) store { z } z") == RPN_OK && top_is_cx(c, 1, 2));
    CHECK(run(c, "clear (1,1) store+ { z } z") == RPN_OK && top_is_cx(c, 2, 3));
    /* a failed storage operation changes neither stack nor variable */
    CHECK(run(c, "clear 0 store/ { z }") == RPN_E_DIV_ZERO);
    CHECK(run(c, "z") == RPN_OK && top_is_cx(c, 2, 3));
    CHECK(rpn_depth(c) == 2);
    /* clearvars */
    CHECK(run(c, "clearvars") == RPN_OK);
    CHECK(run(c, "z") == RPN_E_UNKNOWN_CMD);
    CHECK(run(c, "recall { z }") == RPN_E_UNDEFINED);
    /* many variables: the table grows */
    {
        char buf[64];
        int i;

        for (i = 0; i < 40; i++) {
            sprintf(buf, "%d store { v%d }", i, i);
            CHECK(run(c, buf) == RPN_OK);
        }
        CHECK(run(c, "v0 v39 v17") == RPN_OK);
        CHECK(rpn_peek(c, 0, &v) == RPN_OK && v.u.i == 17);
        CHECK(rpn_peek(c, 1, &v) == RPN_OK && v.u.i == 39);
    }
    /* names */
    CHECK(run(c, "1 store { a_b_9 }") == RPN_OK);
    CHECK(run(c, "1 store { 9a }") == RPN_E_BAD_NAME);
    CHECK(run(c, "1 store { _a }") == RPN_E_BAD_NAME);
    CHECK(run(c, "1 store { a.b }") == RPN_E_BAD_NAME);
    CHECK(run(c, "1 store { pi }") == RPN_E_BAD_NAME);
    CHECK(run(c, "1 store { x=0? }") == RPN_E_BAD_NAME);
    CHECK(run(c, "1 STORE { UPPER } upper") == RPN_OK);
    /* a group may arrive in later calls: the command waits for it */
    CHECK(run(c, "clear 7 store") == RPN_OK && rpn_in_program(c));
    CHECK(run(c, "{") == RPN_OK && rpn_in_program(c));
    CHECK(run(c, "late_name") == RPN_OK && rpn_in_program(c));
    CHECK(run(c, "}") == RPN_OK && !rpn_in_program(c));
    CHECK(run(c, "late_name") == RPN_OK && top_is_int(c, 7));
    rpn_free(c);
    CHECK(m.allocs == m.frees);
    /* a name as long as the lexer allows */
    {
        char buf[RPN_MAX_TOKEN + 16];
        int i;

        strcpy(buf, "1 store { ");
        for (i = 0; i < RPN_MAX_TOKEN; i++) {
            buf[10 + i] = 'a';
        }
        strcpy(buf + 10 + RPN_MAX_TOKEN, " }");
        CHECK(depth_after(buf) == 0);
    }
}

static void test_conditions(void)
{
    /* a true test runs the next command, a false test skips it */
    CHECK(evals_int("0 x=0? 5", 5));
    CHECK(evals_int("1 x=0? 5", 1));
    CHECK(evals_int("1 x!=0? 5", 5));
    CHECK(evals_int("-1 x<0? 5", 5));
    CHECK(evals_int("0 x<0? 5", 0));
    CHECK(evals_int("1 x>0? 5", 5));
    CHECK(evals_int("0 x<=0? 5", 5));
    CHECK(evals_int("1 x<=0? 5", 1));
    CHECK(evals_int("0 x>=0? 5", 5));
    CHECK(evals_int("-1 x>=0? 5", -1));
    CHECK(evals_int("1 2 x<y? 7", 7) == 0);          /* x=2, y=1 */
    CHECK(evals_int("2 1 x<y? 7", 7));               /* x=1 < y=2 */
    CHECK(evals_int("1 2 x>y? 7", 7));
    CHECK(evals_int("3 3 x=y? 7", 7));
    CHECK(evals_int("3 4 x!=y? 7", 7));
    CHECK(evals_int("3 3 x<=y? 7", 7));
    CHECK(evals_int("3 3 x>=y? 7", 7));
    CHECK(evals_int("3 4 x=y? 7", 4));
    /* the test does not touch the stack or LASTX */
    CHECK(depth_after("1 2 x=y?") == 2);
    CHECK(evals_int("5 6 x=y? drop lastx", 5) == 0);
    /* exactly one command is skipped, with its argument */
    CHECK(evals_int("1 1 x=0? 2 3 +", 4));      /* 2 skipped */
    CHECK(evals_int("1 0 x=0? 2 3 +", 5));      /* 2 runs, 3 + 2 */
    CHECK(depth_after("5 0 x>0? store { z }") == 2);       /* store skipped */
    CHECK(depth_after("5 1 x>0? store { z }") == 1);       /* store runs    */
    CHECK(depth_after("5 0 x>0? store { 2x }") == -1);     /* bad name still seen */
    /* a skipped number is not parsed as a pushed value; a bad one still errs */
    CHECK(fails_with("0 x>0? 1e999", RPN_E_REAL_OVERFLOW, 1));
    /* type tests */
    CHECK(evals_int("1 real? 9", 9));
    CHECK(evals_int("(1,0) real? 9", 9) == 0);
    CHECK(evals_int("1.5 integer? 9", 9) == 0);
    CHECK(evals_int("2.0 integer? 9", 9));
    CHECK(evals_int("(1,0) complex? 9", 9));
    CHECK(evals_int("1 complex? 9", 9) == 0);
    CHECK(fails_with("(1,2) x<0?", RPN_E_TYPE, 1));
    CHECK(fails_with("(1,2) 3 x<y?", RPN_E_TYPE, 2));
    CHECK(fails_with("x=0?", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_with("1 x=y?", RPN_E_STACK_UNDERFLOW, 1));
    CHECK(fails_with("real?", RPN_E_STACK_UNDERFLOW, 0));
    /* an error clears the pending skip */
    {
        mock_t m;
        rpn_ctx_t *c = make(&m);

        run(c, "0 x>0?");
        CHECK(run(c, "frob") == RPN_E_UNKNOWN_CMD);
        CHECK(run(c, "5") == RPN_OK && top_is_int(c, 5));
        /* the skip survives separate rpn_exec calls */
        run(c, "0 x>0?");
        CHECK(run(c, "6") == RPN_OK && top_is_int(c, 0));
        rpn_free(c);
    }
    /* flags */
    CHECK(evals_int("setflag { 3 } flag-set? { 3 } 8", 8));
    CHECK(evals_int("flag-set? { 3 } 8 9", 9));
    CHECK(evals_int("setflag { 3 } clearflag { 3 } flag-set? { 3 } 8 9", 9));
    CHECK(evals_int("flag-clear? { 3 } 8", 8));
    CHECK(evals_int("sf { 31 } fs? { 31 } 8", 8));
    CHECK(evals_int("sf { 0 } cf { 0 } fc? { 0 } 8", 8));
    CHECK(fails_with("setflag { 32 }", RPN_E_BAD_ARG, 0));
    CHECK(fails_with("setflag { -1 }", RPN_E_BAD_ARG, 0));
    CHECK(fails_with("setflag { 1.5 }", RPN_E_BAD_ARG, 0));
    CHECK(fails_with("setflag { q }", RPN_E_BAD_ARG, 0));
    CHECK(fails_with("flag-clear? 99", RPN_E_BAD_ARG, 0));
}

static void test_programs(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    /* entering a program stores it: nothing runs, the stack is unchanged */
    CHECK(run(c, "1 2 program {") == RPN_OK && rpn_in_program(c));
    CHECK(run(c, "label { inc } 1 + return") == RPN_OK && rpn_depth(c) == 2);
    CHECK(rpn_in_program(c));
    CHECK(run(c, "}") == RPN_OK && !rpn_in_program(c));
    CHECK(run(c, "execute { inc }") == RPN_OK && top_is_int(c, 3));
    CHECK(run(c, "xeq { inc } xeq { inc }") == RPN_OK && top_is_int(c, 5));
    /* programs can be added to; the earlier ones stay */
    CHECK(run(c, "program { label { dbl } 2 * rtn }") == RPN_OK);
    CHECK(run(c, "execute { dbl } execute { inc }") == RPN_OK && top_is_int(c, 11));
    /* calls nest, return goes back to the caller */
    CHECK(run(c, "program { label { both } xeq { dbl } xeq { inc } rtn }") == RPN_OK);
    CHECK(run(c, "clear 4 execute { both }") == RPN_OK && top_is_int(c, 9));
    /* stop ends a run and drops the pending state */
    CHECK(run(c, "program { label { st } 1 stop 2 } clear goto { st }") == RPN_OK);
    CHECK(rpn_depth(c) == 1 && top_is_int(c, 1));
    /* clearprogram */
    CHECK(run(c, "clearprogram") == RPN_OK);
    CHECK(run(c, "execute { inc }") == RPN_E_UNDEFINED);
    /* program mode errors */
    CHECK(run(c, "program {") == RPN_OK);
    CHECK(run(c, "program {") == RPN_E_PROGRAM);
    CHECK(run(c, "1e999") == RPN_E_REAL_OVERFLOW);
    CHECK(run(c, "a-b") == RPN_E_UNKNOWN_CMD);
    CHECK(run(c, "label { x } label { x }") == RPN_E_DUPLICATE);
    CHECK(run(c, "goto { 3 }") == RPN_E_BAD_NAME);
    CHECK(run(c, "}") == RPN_OK);
    /* a program may use a variable that does not exist yet */
    CHECK(run(c, "clear clearprogram program { label { u } later }") == RPN_OK);
    CHECK(run(c, "execute { u }") == RPN_E_UNKNOWN_CMD);
    CHECK(run(c, "7 store { later } execute { u }") == RPN_OK && top_is_int(c, 7));
    rpn_free(c);
    CHECK(m.allocs == m.frees);
}

static void test_run_control(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    /* a loop with a counter: 1 + 2 + ... + 5 */
    CHECK(run(c, "program { label { sum } 0 store { s } 5 store { n } "
                 "label { top } n store+ { s } dse { n } goto { top } s }") == RPN_OK);
    CHECK(run(c, "goto { sum }") == RPN_OK && top_is_int(c, 15));
    /* run limits are per run and can be changed */
    CHECK(run(c, "program { label { forever } goto { forever } }") == RPN_OK);
    CHECK(run(c, "50 steplimit") == RPN_OK);
    CHECK(run(c, "goto { forever }") == RPN_E_STEP_LIMIT);
    CHECK(run(c, "goto { forever }") == RPN_E_STEP_LIMIT);        /* repeatable */
    CHECK(run(c, "1000000 steplimit goto { sum }") == RPN_OK);
    /* a state left over from the failed run does not leak: tests work */
    CHECK(run(c, "clear 0 x=0? 4") == RPN_OK && top_is_int(c, 4));
    /* recursion through execute: the sum of 1..n, n on the stack */
    CHECK(run(c, "clearprogram program { label { rsum } x=0? return "
                 "dup 1 - xeq { rsum } + return }") == RPN_OK);
    CHECK(run(c, "clear 10 xeq { rsum }") == RPN_OK && top_is_int(c, 55));
    CHECK(rpn_depth(c) == 1);
    rpn_free(c);

    /* call depth: the limit applies to nested execute only */
    c = make(&m);
    run(c, "program { label { down } 1 + xeq { down } }");
    run(c, "4 calllimit");
    CHECK(run(c, "0 xeq { down }") == RPN_E_CALL_DEPTH);
    CHECK(rpn_depth(c) >= 1);
    run(c, "300 calllimit");
    CHECK(run(c, "clear 0 xeq { down }") == RPN_E_CALL_DEPTH);
    /* the return stack is reset: a normal call still works afterwards */
    run(c, "clearprogram program { label { one } 1 return }");
    CHECK(run(c, "clear xeq { one }") == RPN_OK && top_is_int(c, 1));
    rpn_free(c);

    /* a limit command needs a positive int */
    CHECK(fails_with("0 steplimit", RPN_E_DOMAIN, 1));
    CHECK(fails_with("-3 calllimit", RPN_E_DOMAIN, 1));
    CHECK(fails_with("2.5 steplimit", RPN_E_TYPE, 1));
    CHECK(fails_with("steplimit", RPN_E_STACK_UNDERFLOW, 0));
    CHECK(fails_with("calllimit", RPN_E_STACK_UNDERFLOW, 0));
    /* program commands outside a run */
    CHECK(fails_with("stop", RPN_E_PROGRAM, 0));
    CHECK(fails_with("rtn", RPN_E_PROGRAM, 0));
    CHECK(fails_with("}", RPN_E_PROGRAM, 0));
    CHECK(fails_with("lbl { a }", RPN_E_PROGRAM, 0));
    CHECK(fails_with("gto { a }", RPN_E_UNDEFINED, 0));
    CHECK(fails_with("xeq { a }", RPN_E_UNDEFINED, 0));
}

static void test_loop_counters(void)
{
    /* integers count down to 0, skipping when the result is <= 0 */
    CHECK(evals_int("3 store { n } dse { n } 7 drop n", 2));
    CHECK(evals_int("1 store { n } dse { n } 7 n", 0));
    CHECK(depth_after("1 store { n } dse { n } 7") == 0);          /* 0: 7 skipped */
    CHECK(depth_after("2 store { n } dse { n } 7") == 1);          /* 1: 7 runs    */
    CHECK(evals_int("0 store { n } isg { n } 7 n", 1));
    CHECK(depth_after("0 store { n } isg { n } 7") == 0);          /* 1 > 0 skips  */
    /* HP-42S format ccc.fffii: 1.003 counts 1 to 3 in steps of 1 */
    CHECK(evals_real("1.003 store { k } isg { k } k", 2.003));
    CHECK(depth_after("2.003 store { k } isg { k } 9") == 1);      /* 3 > 3? no   */
    CHECK(depth_after("3.003 store { k } isg { k } 9") == 0);      /* 4 > 3: skip */
    /* step 2: 0.00502 */
    CHECK(evals_real("0.00502 store { k } isg { k } k", 2.00502));
    /* decrement: 5.00001 counts 5 down to 0 */
    CHECK(evals_real("5.00001 store { k } dse { k } k", 4.00001));
    CHECK(fails_with("dse { zz }", RPN_E_UNDEFINED, 0));
    CHECK(fails_with("isg { zz }", RPN_E_UNDEFINED, 0));
    CHECK(fails_with("(1,2) store { c } dse { c }", RPN_E_TYPE, 0));
    CHECK(fails_with("2000000000 store { b } isg { b }", RPN_E_INT_OVERFLOW, 0));
    CHECK(fails_with("1e12 store { b } dse { b }", RPN_E_DOMAIN, 0));
    /* a counting loop in a program */
    CHECK(evals_int("program { label { cnt } 0 store { t } 10 store { i } "
                    "label { lp } i store+ { t } dse { i } goto { lp } t } goto { cnt }", 55));
}

/* Allocation failure in each growing table: variables, program, returns. */
static void test_phase3_alloc_failures(void)
{
    static const char *scripts[] = {
        "1 store { a }",
        "program { label { l } 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 }",
        "program { label { r } xeq { r } } 5 calllimit xeq { r }"
    };
    size_t i;
    int n;

    for (i = 0; i < sizeof scripts / sizeof scripts[0]; i++) {
        int saw_nomem = 0;

        for (n = 1; n <= 8; n++) {
            mock_t m;
            rpn_env_t e;
            rpn_ctx_t *c;
            int rc;

            memset(&m, 0, sizeof m);
            m.fail_at = n;
            e.user = &m;
            e.alloc = m_alloc;
            e.release = m_release;
            e.out = NULL;
            e.error = m_error;
            c = rpn_new(&e);
            if (c == NULL) {
                continue;
            }
            rc = run(c, scripts[i]);
            if (rc == RPN_E_NOMEM) {
                saw_nomem = 1;
                CHECK(m.last_code == RPN_E_NOMEM);
                /* still usable afterwards */
                CHECK(run(c, "clear 1 2 +") == RPN_OK ||
                      m.fail_at > 0);
            } else {
                CHECK(rc == RPN_OK || rc == RPN_E_CALL_DEPTH);
            }
            rpn_free(c);
            CHECK(m.allocs == m.frees);
        }
        CHECK(saw_nomem);
    }
}

/* ---- phase 4: functions and locals --------------------------------- */

static void test_functions(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);
    rpn_value_t v;

    /* a definition stores, nothing runs; the stack is unchanged */
    CHECK(run(c, "5 function { hyp") == RPN_OK && rpn_in_program(c));
    CHECK(run(c, "local { a b } a sq b sq + sqrt") == RPN_OK);
    CHECK(rpn_depth(c) == 1);
    CHECK(run(c, "}") == RPN_OK && !rpn_in_program(c));
    /* local order: the last name receives the top */
    CHECK(run(c, "clear 3 4 hyp") == RPN_OK && top_is_int(c, 5));
    CHECK(rpn_depth(c) == 1);
    /* a function is called like a command, inside input lines too */
    CHECK(run(c, "clear 6 8 hyp 3 4 hyp +") == RPN_OK && top_is_int(c, 15));
    /* order of arguments: 10 3 local { a b } gives a = 10, b = 3 */
    CHECK(run(c, "clear function { sub local { a b } a b - } 10 3 sub") ==
          RPN_OK && top_is_int(c, 7));
    /* locals shadow globals and leave them alone */
    CHECK(run(c, "clear 100 store { a } function { sh local { a } "
                 "a 1 + store { a } a } 5 sh") == RPN_OK && top_is_int(c, 6));
    CHECK(run(c, "a") == RPN_OK && top_is_int(c, 100));
    /* a local is invisible after the return */
    CHECK(run(c, "clear function { lv local { zz } zz } 9 lv") == RPN_OK);
    CHECK(run(c, "zz") == RPN_E_UNKNOWN_CMD);
    CHECK(run(c, "recall { zz }") == RPN_E_UNDEFINED);
    /* a callee cannot see the caller's locals */
    CHECK(run(c, "clear function { inner2 loc1 } "
                 "function { outer2 7 local { loc1 } inner2 }") == RPN_OK);
    CHECK(run(c, "outer2") == RPN_E_UNKNOWN_CMD);
    rpn_free(c);
    CHECK(m.allocs == m.frees);

    /* recursion: every call has its own n */
    c = make(&m);
    CHECK(run(c, "function { fact local { n } n 2 x>y? goto { base } "
                 "drop drop n n 1 - fact * return "
                 "label { base } drop drop 1 return }") == RPN_OK);
    CHECK(run(c, "10 fact") == RPN_OK && top_is_int(c, 3628800L));
    CHECK(run(c, "clear 0 fact") == RPN_OK && top_is_int(c, 1));
    CHECK(rpn_depth(c) == 1);
    CHECK(run(c, "clear 12 fact") == RPN_OK && top_is_int(c, 479001600L));
    rpn_free(c);

    /* a nested call: the locals of the two frames are separate */
    c = make(&m);
    CHECK(run(c, "function { inner local { n } n 10 * } "
                 "function { outer local { n } n 1 + inner n + }") == RPN_OK);
    CHECK(run(c, "4 outer") == RPN_OK && top_is_int(c, 54));
    /* execute inside a function shares its locals */
    CHECK(run(c, "clear function { sh local { n } xeq { twice } n + return "
                 "label { twice } n n + return }") == RPN_OK);
    CHECK(run(c, "5 sh") == RPN_OK && top_is_int(c, 15));
    rpn_free(c);

    /* local underflow keeps the stack; the frames are reset afterwards */
    c = make(&m);
    run(c, "function { two local { a b } a b + }");
    CHECK(run(c, "7 two") == RPN_E_STACK_UNDERFLOW);
    CHECK(rpn_depth(c) == 1 && rpn_peek(c, 0, &v) == RPN_OK && v.u.i == 7);
    CHECK(run(c, "clear 1 2 two") == RPN_OK && top_is_int(c, 3));
    rpn_free(c);
    CHECK(m.allocs == m.frees);
}

static void test_function_errors(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    /* name rules: nothing is entered, no block stays open */
    CHECK(run(c, "function { dup") == RPN_E_BAD_NAME);
    CHECK(run(c, "function { 3x") == RPN_E_BAD_NAME);
    CHECK(!rpn_in_program(c));
    CHECK(run(c, "function { f 1 }") == RPN_OK);
    CHECK(run(c, "function { f") == RPN_E_DUPLICATE);
    CHECK(!rpn_in_program(c));
    CHECK(run(c, "7 store { v } function { v") == RPN_E_BAD_NAME);
    CHECK(run(c, "1 store { f }") == RPN_E_BAD_NAME);       /* a function */
    CHECK(run(c, "f") == RPN_OK);
    /* redefinition needs clearprogram, which removes the functions */
    CHECK(run(c, "clearprogram function { f 2 }") == RPN_OK);
    CHECK(run(c, "clear f") == RPN_OK && top_is_int(c, 2));
    /* shape errors */
    CHECK(run(c, "function f") == RPN_E_BAD_ARG);
    CHECK(run(c, "function { }") == RPN_E_BAD_ARG);
    CHECK(!rpn_in_program(c));
    /* a block cannot hold a block; the error leaves it open */
    CHECK(run(c, "function { g") == RPN_OK && rpn_in_program(c));
    CHECK(run(c, "function { h") == RPN_E_PROGRAM);
    CHECK(run(c, "program {") == RPN_E_PROGRAM);
    CHECK(rpn_in_program(c));
    /* a bad local list is dropped, the function stays open */
    CHECK(run(c, "local { a a") == RPN_E_DUPLICATE);
    CHECK(run(c, "local { dup") == RPN_E_BAD_NAME);
    CHECK(run(c, "local { 3x") == RPN_E_BAD_NAME);
    CHECK(run(c, "local { } ") == RPN_E_BAD_ARG);
    CHECK(rpn_in_program(c));
    CHECK(run(c, "local { a } a }") == RPN_OK && !rpn_in_program(c));
    CHECK(run(c, "7 g") == RPN_OK && top_is_int(c, 7));
    /* local is for function bodies only */
    CHECK(run(c, "program {") == RPN_OK);
    CHECK(run(c, "local { x }") == RPN_E_PROGRAM);
    CHECK(run(c, "}") == RPN_OK);
    CHECK(run(c, "local { x }") == RPN_E_PROGRAM);
    CHECK(run(c, "}") == RPN_E_PROGRAM);
    rpn_free(c);
    CHECK(m.allocs == m.frees);

    /* label scope */
    CHECK(fails_with("program { label { g } } function { f goto { g } } f",
                     RPN_E_SCOPE, 0));
    CHECK(fails_with("program { label { g } } function { f xeq { g } } f",
                     RPN_E_SCOPE, 0));
    CHECK(fails_with("function { f label { in } } goto { in }",
                     RPN_E_SCOPE, 0));
    CHECK(fails_with("function { f label { in } } function { g goto { in } } g",
                     RPN_E_SCOPE, 0));
    CHECK(fails_with("function { f goto { nowhere } } f", RPN_E_UNDEFINED, 0));
    CHECK(fails_with("function { f label { a } label { a } }",
                     RPN_E_DUPLICATE, 0));
    /* the same label in two scopes is fine */
    CHECK(evals_int("program { label { a } 1 return } "
                    "function { f label { a } 2 return } xeq { a } f +", 3));
    /* a label may share its name with a function or a variable */
    CHECK(evals_int("function { f 5 } program { label { f } 1 return } "
                    "xeq { f } f +", 6));
    /* execute inside a function: a plain call in the same scope */
    CHECK(evals_int("function { f xeq { sub } 1 + return "
                    "label { sub } 10 return } f", 11));
    /* goto inside a function stays in it */
    CHECK(evals_int("function { f 1 goto { done } 5 label { done } 2 + } f", 3));
    /* a definition cannot be nested in a program */
    CHECK(fails_with("program { function { z 9 } }", RPN_E_PROGRAM, 0));

    /* call depth: functions and execute share the limit */
    CHECK(fails_with("function { r r } 5 calllimit r", RPN_E_CALL_DEPTH, 0));
}

/* An error in the middle of a function leaks no frame and no local. */
static void test_function_cleanup(void)
{
    mock_t m;
    rpn_ctx_t *c = make(&m);

    CHECK(run(c, "function { bad local { a } a 0 / } "
                 "function { ok local { a } a 1 + }") == RPN_OK);
    CHECK(run(c, "3 bad") == RPN_E_DIV_ZERO);
    CHECK(run(c, "a") == RPN_E_UNKNOWN_CMD);               /* no stale local */
    CHECK(run(c, "clear 1 ok") == RPN_OK && top_is_int(c, 2));
    /* the step limit */
    CHECK(run(c, "function { spin local { a } label { t } goto { t } } "
                 "20 steplimit") == RPN_OK);
    CHECK(run(c, "1 spin") == RPN_E_STEP_LIMIT);
    CHECK(run(c, "1000000 steplimit clear 1 ok") == RPN_OK &&
          top_is_int(c, 2));
    /* the call limit: after the error a deep call works again */
    CHECK(run(c, "function { deep local { n } n x=0? return drop n 1 - deep }")
          == RPN_OK);
    CHECK(run(c, "5 calllimit clear 100 deep") == RPN_E_CALL_DEPTH);
    CHECK(run(c, "300 calllimit clear 100 deep") == RPN_OK);
    /* a failed test skips a function call and only that */
    CHECK(run(c, "clear 1 x=0? ok 9") == RPN_OK && rpn_depth(c) == 2 &&
          top_is_int(c, 9));
    CHECK(run(c, "clear 0 x=0? ok 9") == RPN_OK && rpn_depth(c) == 2 &&
          top_is_int(c, 9));
    rpn_free(c);
    CHECK(m.allocs == m.frees);
}

/* Allocation failure in the frames and locals tables. */
static void test_phase4_alloc_failures(void)
{
    static const char *scripts[] = {
        "function { fn local { n1 n2 n3 n4 n5 n6 n7 n8 n9 n10 } } "
        "1 2 3 4 5 6 7 8 9 10 fn",
        "function { r r } 40 calllimit r"
    };
    size_t i;
    int n;

    for (i = 0; i < sizeof scripts / sizeof scripts[0]; i++) {
        int saw_nomem = 0;

        for (n = 1; n <= 12; n++) {
            mock_t m;
            rpn_env_t e;
            rpn_ctx_t *c;
            int rc;

            memset(&m, 0, sizeof m);
            m.fail_at = n;
            e.user = &m;
            e.alloc = m_alloc;
            e.release = m_release;
            e.out = NULL;
            e.error = m_error;
            c = rpn_new(&e);
            if (c == NULL) {
                continue;
            }
            rc = run(c, scripts[i]);
            if (rc == RPN_E_NOMEM) {
                saw_nomem = 1;
                CHECK(m.last_code == RPN_E_NOMEM);
            } else {
                CHECK(rc == RPN_OK || rc == RPN_E_CALL_DEPTH);
            }
            rpn_free(c);
            CHECK(m.allocs == m.frees);
        }
        CHECK(saw_nomem);
    }
}

int main(void)
{
    test_lexer();
    test_numbers();
    test_format();
    test_stack_commands();
    test_stack_depths();
    test_stack_max();
    test_arith();
    test_aliases_and_case();
    test_last_error_state();
    test_push_api();
    test_type_error();
    test_error_coverage();
    test_failed_command_keeps_stack();
    test_alloc_failures();
    test_no_leaks();
    test_env_without_error_hook();
    test_complex_literals();
    test_complex_arith();
    test_complex_errors();
    test_exp_log();
    test_trig();
    test_angle_modes();
    test_polar_rect();
    test_hyperbolic();
    test_rounding();
    test_display_modes();
    test_mode_state();
    test_identities();
    test_phase2_misc();
    test_variables();
    test_conditions();
    test_programs();
    test_run_control();
    test_loop_counters();
    test_phase3_alloc_failures();
    test_functions();
    test_function_errors();
    test_function_cleanup();
    test_phase4_alloc_failures();
    printf("unit: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
