/*
 * arith.c - arithmetic commands: + - * / neg inv abs mod sq sqrt pow percent.
 *
 * Conventions
 *   - For a binary command, x is the top of the stack and y the value
 *     below it, so "a b -" computes a - b.  Both operands are replaced by
 *     the result.
 *   - Int op int stays int when the result is exact and representable;
 *     otherwise an int overflow is an error (never a silent wrap).  Any
 *     real operand promotes the result to real.
 *   - "/" of two ints gives an int when the division is exact, else a real.
 *   - mod follows the sign of the divisor (floored), like the HP-42S.
 *   - Results are computed first and written back last, so a failing
 *     command leaves the stack unchanged.
 */
#include <math.h>
#include "intern.h"

typedef int (*binfn_t)(const rpn_value_t *y, const rpn_value_t *x,
                       rpn_value_t *out);
typedef int (*unfn_t)(const rpn_value_t *x, rpn_value_t *out);
typedef int (*cxbin_t)(cx_t *r, const cx_t *a, const cx_t *b);

/* ---- helpers ----------------------------------------------------- */

#define is_num rpn_is_num

/* Applies a complex binary operation to two numbers of any kind. */
static int cx_binary(cxbin_t op, const rpn_value_t *y, const rpn_value_t *x,
                     rpn_value_t *o)
{
    cx_t a, b, r;
    int rc;

    rpn_to_cx(y, &a);
    rpn_to_cx(x, &b);
    rc = op(&r, &a, &b);
    return rc != RPN_OK ? rc : rpn_ret_cx(o, &r);
}

static double to_d(const rpn_value_t *v)
{
    return v->tag == RPN_T_INT ? (double)v->u.i : v->u.r;
}

/* Stores a real result, rejecting NaN and infinities. */
static int set_real(rpn_value_t *out, double r)
{
    if (RPN_ISNAN(r)) {
        return RPN_E_DOMAIN;
    }
    if (RPN_ISINF(r)) {
        return RPN_E_REAL_OVERFLOW;
    }
    *out = rpn_value_real(r);
    return RPN_OK;
}

static int is_integral(double d)
{
    return floor(d) == d;
}

/* Checked long arithmetic. Each returns RPN_OK or RPN_E_INT_OVERFLOW. */
static int l_add(long a, long b, long *r)
{
    if ((b > 0 && a > LONG_MAX - b) || (b < 0 && a < LONG_MIN - b)) {
        return RPN_E_INT_OVERFLOW;
    }
    *r = a + b;
    return RPN_OK;
}

static int l_sub(long a, long b, long *r)
{
    if ((b < 0 && a > LONG_MAX + b) || (b > 0 && a < LONG_MIN + b)) {
        return RPN_E_INT_OVERFLOW;
    }
    *r = a - b;
    return RPN_OK;
}

static int l_mul(long a, long b, long *r)
{
    if (a == 0 || b == 0) {
        *r = 0;
        return RPN_OK;
    }
    if (a > 0) {
        if (b > 0 ? a > LONG_MAX / b : b < LONG_MIN / a) {
            return RPN_E_INT_OVERFLOW;
        }
    } else {
        if (b > 0 ? a < LONG_MIN / b : b < LONG_MAX / a) {
            return RPN_E_INT_OVERFLOW;
        }
    }
    *r = a * b;
    return RPN_OK;
}

/* Applies a binary function to y (below) and x (top). */
static int binop(rpn_ctx_t *c, binfn_t fn)
{
    rpn_value_t res;
    int rc;

    if (!is_num(&STK_AT(c, 0)) || !is_num(&STK_AT(c, 1))) {
        return RPN_E_TYPE;
    }
    rc = fn(&STK_AT(c, 1), &STK_AT(c, 0), &res);
    if (rc != RPN_OK) {
        return rc;
    }
    c->depth--;
    STK_AT(c, 0) = res;
    return RPN_OK;
}

static int unop(rpn_ctx_t *c, unfn_t fn)
{
    rpn_value_t res;
    int rc;

    if (!is_num(&STK_AT(c, 0))) {
        return RPN_E_TYPE;
    }
    rc = fn(&STK_AT(c, 0), &res);
    if (rc != RPN_OK) {
        return rc;
    }
    STK_AT(c, 0) = res;
    return RPN_OK;
}

/* ---- binary operations ------------------------------------------- */

static int f_add(const rpn_value_t *y, const rpn_value_t *x, rpn_value_t *o)
{
    long r;
    int rc;

    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return cx_binary(cx_add, y, x, o);
    }
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT) {
        rc = l_add(y->u.i, x->u.i, &r);
        if (rc == RPN_OK) {
            *o = rpn_value_int(r);
        }
        return rc;
    }
    return set_real(o, to_d(y) + to_d(x));
}

static int f_sub(const rpn_value_t *y, const rpn_value_t *x, rpn_value_t *o)
{
    long r;
    int rc;

    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return cx_binary(cx_sub, y, x, o);
    }
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT) {
        rc = l_sub(y->u.i, x->u.i, &r);
        if (rc == RPN_OK) {
            *o = rpn_value_int(r);
        }
        return rc;
    }
    return set_real(o, to_d(y) - to_d(x));
}

static int f_mul(const rpn_value_t *y, const rpn_value_t *x, rpn_value_t *o)
{
    long r;
    int rc;

    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return cx_binary(cx_mul, y, x, o);
    }
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT) {
        rc = l_mul(y->u.i, x->u.i, &r);
        if (rc == RPN_OK) {
            *o = rpn_value_int(r);
        }
        return rc;
    }
    return set_real(o, to_d(y) * to_d(x));
}

static int f_div(const rpn_value_t *y, const rpn_value_t *x, rpn_value_t *o)
{
    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return cx_binary(cx_div, y, x, o);
    }
    if (to_d(x) == 0.0) {
        return RPN_E_DIV_ZERO;
    }
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT) {
        if (x->u.i == -1) {
            if (y->u.i == LONG_MIN) {
                return RPN_E_INT_OVERFLOW;
            }
            *o = rpn_value_int(-y->u.i);
            return RPN_OK;
        }
        if (y->u.i % x->u.i == 0) {
            *o = rpn_value_int(y->u.i / x->u.i);
            return RPN_OK;
        }
    }
    return set_real(o, to_d(y) / to_d(x));
}

static int f_mod(const rpn_value_t *y, const rpn_value_t *x, rpn_value_t *o)
{
    long r;
    double d, a, b;

    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return RPN_E_TYPE;
    }
    if (to_d(x) == 0.0) {
        return RPN_E_DIV_ZERO;
    }
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT) {
        if (x->u.i == -1) {
            *o = rpn_value_int(0L);
            return RPN_OK;
        }
        r = y->u.i % x->u.i;
        if (r != 0 && ((r < 0) != (x->u.i < 0))) {
            r += x->u.i;
        }
        *o = rpn_value_int(r);
        return RPN_OK;
    }
    a = to_d(y);
    b = to_d(x);
    d = fmod(a, b);
    if (d != 0.0 && ((d < 0.0) != (b < 0.0))) {
        d += b;
    }
    return set_real(o, d);
}

/* Integer power by repeated squaring; e >= 0. */
static int l_pow(long base, long e, long *r)
{
    long acc = 1;
    int rc;

    while (e > 0) {
        if (e & 1) {
            rc = l_mul(acc, base, &acc);
            if (rc != RPN_OK) {
                return rc;
            }
        }
        e >>= 1;
        if (e > 0) {
            rc = l_mul(base, base, &base);
            if (rc != RPN_OK) {
                return rc;
            }
        }
    }
    *r = acc;
    return RPN_OK;
}

/* A negative base with a non-integer exponent gives a complex result,
 * or a domain error in strict mode. */
static int f_pow(rpn_ctx_t *c, const rpn_value_t *y, const rpn_value_t *x,
                 rpn_value_t *o)
{
    double b, e;
    long r;
    int rc;

    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return cx_binary(cx_pow, y, x, o);
    }
    b = to_d(y);
    e = to_d(x);
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT) {
        if (x->u.i >= 0) {
            rc = l_pow(y->u.i, x->u.i, &r);
            if (rc == RPN_OK) {
                *o = rpn_value_int(r);
            }
            return rc;
        }
        if (y->u.i == 1) {
            *o = rpn_value_int(1L);
            return RPN_OK;
        }
        if (y->u.i == -1) {
            *o = rpn_value_int((x->u.i % 2 == 0) ? 1L : -1L);
            return RPN_OK;
        }
    }
    if (b == 0.0 && e < 0.0) {
        return RPN_E_DIV_ZERO;
    }
    if (b < 0.0 && !is_integral(e)) {
        if (c->strict) {
            return RPN_E_DOMAIN;
        }
        return cx_binary(cx_pow, y, x, o);
    }
    return set_real(o, pow(b, e));
}

/* x percent of y: y * x / 100.  The command keeps y on the stack. */
static int f_percent_calc(const rpn_value_t *y, const rpn_value_t *x,
                          rpn_value_t *o)
{
    long prod;

    if (y->tag == RPN_T_COMPLEX || x->tag == RPN_T_COMPLEX) {
        return RPN_E_TYPE;
    }
    if (y->tag == RPN_T_INT && x->tag == RPN_T_INT &&
        l_mul(y->u.i, x->u.i, &prod) == RPN_OK && prod % 100 == 0) {
        *o = rpn_value_int(prod / 100);
        return RPN_OK;
    }
    return set_real(o, to_d(y) * to_d(x) / 100.0);
}

/* ---- unary operations -------------------------------------------- */

static int f_neg(const rpn_value_t *x, rpn_value_t *o)
{
    if (x->tag == RPN_T_COMPLEX) {
        *o = rpn_value_complex(-x->u.c.re, -x->u.c.im);
        return RPN_OK;
    }
    if (x->tag == RPN_T_INT) {
        if (x->u.i == LONG_MIN) {
            return RPN_E_INT_OVERFLOW;
        }
        *o = rpn_value_int(-x->u.i);
        return RPN_OK;
    }
    *o = rpn_value_real(-x->u.r);
    return RPN_OK;
}

static int f_inv(const rpn_value_t *x, rpn_value_t *o)
{
    cx_t one, z, r;
    int rc;

    if (x->tag == RPN_T_COMPLEX) {
        cx_set(&one, 1.0, 0.0);
        rpn_to_cx(x, &z);
        rc = cx_div(&r, &one, &z);
        return rc != RPN_OK ? rc : rpn_ret_cx(o, &r);
    }
    if (to_d(x) == 0.0) {
        return RPN_E_DIV_ZERO;
    }
    if (x->tag == RPN_T_INT && (x->u.i == 1 || x->u.i == -1)) {
        *o = *x;
        return RPN_OK;
    }
    return set_real(o, 1.0 / to_d(x));
}

static int f_abs(const rpn_value_t *x, rpn_value_t *o)
{
    cx_t z;

    if (x->tag == RPN_T_COMPLEX) {
        rpn_to_cx(x, &z);
        return set_real(o, cx_abs(&z));
    }
    if (x->tag == RPN_T_INT) {
        if (x->u.i >= 0) {
            *o = *x;
            return RPN_OK;
        }
        return f_neg(x, o);
    }
    *o = rpn_value_real(fabs(x->u.r));
    return RPN_OK;
}

static int f_sq(const rpn_value_t *x, rpn_value_t *o)
{
    long r;
    int rc;
    cx_t z;

    if (x->tag == RPN_T_COMPLEX) {
        rpn_to_cx(x, &z);
        rc = cx_mul(&z, &z, &z);
        return rc != RPN_OK ? rc : rpn_ret_cx(o, &z);
    }
    if (x->tag == RPN_T_INT) {
        rc = l_mul(x->u.i, x->u.i, &r);
        if (rc == RPN_OK) {
            *o = rpn_value_int(r);
        }
        return rc;
    }
    return set_real(o, x->u.r * x->u.r);
}

/*
 * Perfect squares of ints stay int.  A negative real gives a complex
 * result, or a domain error in strict mode.
 */
static int f_sqrt(rpn_ctx_t *c, const rpn_value_t *x, rpn_value_t *o)
{
    unsigned long n, s;
    cx_t z;
    int rc;

    if (x->tag == RPN_T_COMPLEX || to_d(x) < 0.0) {
        if (x->tag != RPN_T_COMPLEX && c->strict) {
            return RPN_E_DOMAIN;
        }
        rpn_to_cx(x, &z);
        rc = cx_sqrt(&z, &z);
        return rc != RPN_OK ? rc : rpn_ret_cx(o, &z);
    }
    if (x->tag == RPN_T_INT) {
        n = (unsigned long)x->u.i;
        s = (unsigned long)sqrt((double)n);
        while (s > 0 && s > n / s) {
            s--;
        }
        while ((s + 1) <= n / (s + 1)) {
            s++;
        }
        if (s * s == n) {
            *o = rpn_value_int((long)s);
            return RPN_OK;
        }
    }
    return set_real(o, sqrt(to_d(x)));
}

/* ---- command handlers -------------------------------------------- */

static int c_add(rpn_ctx_t *c) { return binop(c, f_add); }
static int c_sub(rpn_ctx_t *c) { return binop(c, f_sub); }
static int c_mul(rpn_ctx_t *c) { return binop(c, f_mul); }
static int c_div(rpn_ctx_t *c) { return binop(c, f_div); }
static int c_mod(rpn_ctx_t *c) { return binop(c, f_mod); }
static int c_neg(rpn_ctx_t *c) { return unop(c, f_neg); }
static int c_inv(rpn_ctx_t *c) { return unop(c, f_inv); }
static int c_abs(rpn_ctx_t *c) { return unop(c, f_abs); }
static int c_sq(rpn_ctx_t *c) { return unop(c, f_sq); }

/* pow and sqrt consult the strict flag, so they cannot use binop/unop. */
static int c_pow(rpn_ctx_t *c)
{
    rpn_value_t res;
    int rc;

    if (!is_num(&STK_AT(c, 0)) || !is_num(&STK_AT(c, 1))) {
        return RPN_E_TYPE;
    }
    rc = f_pow(c, &STK_AT(c, 1), &STK_AT(c, 0), &res);
    if (rc != RPN_OK) {
        return rc;
    }
    c->depth--;
    STK_AT(c, 0) = res;
    return RPN_OK;
}

static int c_sqrt(rpn_ctx_t *c)
{
    rpn_value_t res;
    int rc;

    if (!is_num(&STK_AT(c, 0))) {
        return RPN_E_TYPE;
    }
    rc = f_sqrt(c, &STK_AT(c, 0), &res);
    if (rc != RPN_OK) {
        return rc;
    }
    STK_AT(c, 0) = res;
    return RPN_OK;
}

/* Replaces x by x% of y and keeps y. */
static int c_percent(rpn_ctx_t *c)
{
    rpn_value_t res;
    int rc;

    if (!is_num(&STK_AT(c, 0)) || !is_num(&STK_AT(c, 1))) {
        return RPN_E_TYPE;
    }
    rc = f_percent_calc(&STK_AT(c, 1), &STK_AT(c, 0), &res);
    if (rc != RPN_OK) {
        return rc;
    }
    STK_AT(c, 0) = res;
    return RPN_OK;
}

/* y op x on two numbers, without touching the stack. */
int rpn_binary_op(int op, const rpn_value_t *y, const rpn_value_t *x,
                  rpn_value_t *out)
{
    if (!is_num(y) || !is_num(x)) {
        return RPN_E_TYPE;
    }
    switch (op) {
    case '+':
        return f_add(y, x, out);
    case '-':
        return f_sub(y, x, out);
    case '*':
        return f_mul(y, x, out);
    case '/':
        return f_div(y, x, out);
    default:
        break;
    }
    return RPN_E_BAD_ARG;
}

const rpn_cmd_t rpn_arith_cmds[] = {
    { "+",       NULL,  2, CMD_LASTX, c_add     },
    { "-",       NULL,  2, CMD_LASTX, c_sub     },
    { "*",       NULL,  2, CMD_LASTX, c_mul     },
    { "/",       NULL,  2, CMD_LASTX, c_div     },
    { "neg",     "chs", 1, CMD_LASTX, c_neg     },
    { "inv",     "1/x", 1, CMD_LASTX, c_inv     },
    { "abs",     NULL,  1, CMD_LASTX, c_abs     },
    { "mod",     NULL,  2, CMD_LASTX, c_mod     },
    { "sq",      "x^2", 1, CMD_LASTX, c_sq      },
    { "sqrt",    NULL,  1, CMD_LASTX, c_sqrt    },
    { "pow",     "y^x", 2, CMD_LASTX, c_pow     },
    { "percent", "%",   2, CMD_LASTX, c_percent },
    { NULL,      NULL,  0, 0,         NULL      }
};
