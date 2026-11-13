/*
 * mathfn.c - exponential, logarithmic, trigonometric and hyperbolic
 * functions, complex helpers, rounding, constants and the angle, display
 * and domain modes.
 *
 * Conventions
 *   - A function accepts int, real and complex arguments.  A real argument
 *     inside the real domain gives a real result.  Outside it (for example
 *     ln of -1, asin of 2) the result is complex, or RPN_E_DOMAIN when the
 *     "strict" mode is on.  A complex argument always gives a complex
 *     result, which is never demoted automatically (see "demote").
 *   - The angle mode (radians, degrees, grads) applies to real arguments
 *     of sin, cos, tan and to the real results of asin, acos, atan, atan2,
 *     arg and polar.  Complex arguments and results are always in radians.
 *   - In degrees and grads the multiples of a quarter turn are exact:
 *     180 sin is 0, 90 cos is 0.  tan of an odd quarter turn is a domain
 *     error.
 *   - C89 has no log1p, expm1, log2, asinh, acosh or atanh: small
 *     fallbacks are defined here.
 *   - Results are computed first and written back last, so a failing
 *     command leaves the stack unchanged.
 */
#include <math.h>
#include "intern.h"

#define MF_PI 3.14159265358979323846
#define MF_E 2.71828182845904523536
#define MF_LN2 0.69314718055994530942
#define MF_LN10 2.30258509299404568402

typedef double (*rfn_t)(double x);
typedef int (*dom_t)(double x);
typedef int (*cfn_t)(cx_t *r, const cx_t *z);

/* ---- real fallbacks ------------------------------------------------ */

/* Kahan's trick: accurate for tiny x without log1p. */
static double r_log1p(double x)
{
    double u = 1.0 + x;

    if (u == 1.0) {
        return x;
    }
    return log(u) * x / (u - 1.0);
}

static double r_expm1(double x)
{
    double u = exp(x);

    if (u == 1.0) {
        return x;
    }
    if (RPN_ISINF(u)) {
        return u;
    }
    if (u - 1.0 == -1.0) {
        return -1.0;
    }
    return (u - 1.0) * x / log(u);
}

/* log2, exact on powers of two. */
static double r_log2(double x)
{
    double r = log(x) / MF_LN2;
    double n = floor(r + 0.5);

    if (n > -1000.0 && n < 1000.0 && pow(2.0, n) == x) {
        return n;
    }
    return r;
}

static double r_pow10(double x)
{
    return pow(10.0, x);
}

static double r_pow2(double x)
{
    return pow(2.0, x);
}

static double r_asinh(double x)
{
    double a = fabs(x);
    double r;

    if (a > 1e8) {
        r = log(a) + MF_LN2;
    } else {
        r = r_log1p(a + a * a / (1.0 + sqrt(1.0 + a * a)));
    }
    return (x < 0.0) ? -r : r;
}

/* x >= 1 */
static double r_acosh(double x)
{
    double t;

    if (x > 1e8) {
        return log(x) + MF_LN2;
    }
    t = x - 1.0;
    return r_log1p(t + sqrt(2.0 * t + t * t));
}

/* |x| < 1 */
static double r_atanh(double x)
{
    return 0.5 * r_log1p(2.0 * x / (1.0 - x));
}

/* ---- complex variants of the derived functions --------------------- */

static int mf_log10(cx_t *r, const cx_t *z)
{
    int rc = cx_log(r, z);

    if (rc == RPN_OK) {
        cx_set(r, r->re / MF_LN10, r->im / MF_LN10);
    }
    return rc;
}

static int mf_log2(cx_t *r, const cx_t *z)
{
    int rc = cx_log(r, z);

    if (rc == RPN_OK) {
        cx_set(r, r->re / MF_LN2, r->im / MF_LN2);
    }
    return rc;
}

static int mf_log1p(cx_t *r, const cx_t *z)
{
    cx_t one;
    int rc;

    cx_set(&one, 1.0, 0.0);
    rc = cx_add(r, z, &one);
    return rc != RPN_OK ? rc : cx_log(r, r);
}

static int mf_expm1(cx_t *r, const cx_t *z)
{
    cx_t one;
    int rc;

    cx_set(&one, 1.0, 0.0);
    rc = cx_exp(r, z);
    return rc != RPN_OK ? rc : cx_sub(r, r, &one);
}

static int mf_pow10(cx_t *r, const cx_t *z)
{
    cx_t k;

    cx_set(&k, MF_LN10, 0.0);
    return cx_mul(r, z, &k) != RPN_OK ? RPN_E_REAL_OVERFLOW : cx_exp(r, r);
}

static int mf_pow2(cx_t *r, const cx_t *z)
{
    cx_t k;

    cx_set(&k, MF_LN2, 0.0);
    return cx_mul(r, z, &k) != RPN_OK ? RPN_E_REAL_OVERFLOW : cx_exp(r, r);
}

/* ---- domains of the real functions ---------------------------------- */

static int ok_pos(double x)     { return x > 0.0; }
static int ok_gt_m1(double x)   { return x > -1.0; }
static int ok_ge_1(double x)    { return x >= 1.0; }
static int ok_unit(double x)    { return x >= -1.0 && x <= 1.0; }
static int ok_open_unit(double x) { return x > -1.0 && x < 1.0; }

/* ---- generic unary function ----------------------------------------- */

static double rad_to_angle(const rpn_ctx_t *c, double r)
{
    if (c->angle == ANGLE_DEG) {
        return r * 180.0 / MF_PI;
    }
    if (c->angle == ANGLE_GRAD) {
        return r * 200.0 / MF_PI;
    }
    return r;
}

/*
 * Applies the real function rf (when the argument is real and in the
 * domain `ok`; NULL means everywhere) or the complex function cf.  When
 * `angle` is set, a real result is converted from radians to the angle
 * mode.
 */
static int unary_math(rpn_ctx_t *c, rfn_t rf, dom_t ok, cfn_t cf, int angle)
{
    rpn_value_t res;
    cx_t z;
    double d;
    int rc;

    if (!rpn_is_num(&STK_AT(c, 0))) {
        return RPN_E_TYPE;
    }
    if (STK_AT(c, 0).tag != RPN_T_COMPLEX) {
        d = rpn_to_d(&STK_AT(c, 0));
        if (!ok || ok(d)) {
            d = rf(d);
            if (angle) {
                d = rad_to_angle(c, d);
            }
            rc = rpn_ret_real(&res, d);
            if (rc == RPN_OK) {
                STK_AT(c, 0) = res;
            }
            return rc;
        }
        if (c->strict) {
            return RPN_E_DOMAIN;
        }
    }
    rpn_to_cx(&STK_AT(c, 0), &z);
    rc = cf(&z, &z);
    if (rc == RPN_OK) {
        rc = rpn_ret_cx(&res, &z);
    }
    if (rc == RPN_OK) {
        STK_AT(c, 0) = res;
    }
    return rc;
}

/* ---- exponential and logarithmic ------------------------------------ */

static int c_exp(rpn_ctx_t *c)   { return unary_math(c, exp, NULL, cx_exp, 0); }
static int c_ln(rpn_ctx_t *c)    { return unary_math(c, log, ok_pos, cx_log, 0); }
static int c_log(rpn_ctx_t *c)   { return unary_math(c, log10, ok_pos, mf_log10, 0); }
static int c_log2(rpn_ctx_t *c)  { return unary_math(c, r_log2, ok_pos, mf_log2, 0); }
static int c_ln1p(rpn_ctx_t *c)  { return unary_math(c, r_log1p, ok_gt_m1, mf_log1p, 0); }
static int c_expm1(rpn_ctx_t *c) { return unary_math(c, r_expm1, NULL, mf_expm1, 0); }
static int c_pow10(rpn_ctx_t *c) { return unary_math(c, r_pow10, NULL, mf_pow10, 0); }
static int c_pow2(rpn_ctx_t *c)  { return unary_math(c, r_pow2, NULL, mf_pow2, 0); }

/* ---- hyperbolic ----------------------------------------------------- */

static int c_sinh(rpn_ctx_t *c)  { return unary_math(c, sinh, NULL, cx_sinh, 0); }
static int c_cosh(rpn_ctx_t *c)  { return unary_math(c, cosh, NULL, cx_cosh, 0); }
static int c_tanh(rpn_ctx_t *c)  { return unary_math(c, tanh, NULL, cx_tanh, 0); }
static int c_asinh(rpn_ctx_t *c) { return unary_math(c, r_asinh, NULL, cx_asinh, 0); }
static int c_acosh(rpn_ctx_t *c) { return unary_math(c, r_acosh, ok_ge_1, cx_acosh, 0); }
static int c_atanh(rpn_ctx_t *c) { return unary_math(c, r_atanh, ok_open_unit, cx_atanh, 0); }

/* ---- inverse trigonometric (result in the angle mode) ---------------- */

static int c_asin(rpn_ctx_t *c)  { return unary_math(c, asin, ok_unit, cx_asin, 1); }
static int c_acos(rpn_ctx_t *c)  { return unary_math(c, acos, ok_unit, cx_acos, 1); }
static int c_atan(rpn_ctx_t *c)  { return unary_math(c, atan, NULL, cx_atan, 1); }

/* ---- trigonometric (argument in the angle mode) ----------------------- */

#define TR_SIN 0
#define TR_COS 1
#define TR_TAN 2

static const double quad_sin[4] = { 0.0, 1.0, 0.0, -1.0 };
static const double quad_cos[4] = { 1.0, 0.0, -1.0, 0.0 };

/* Real trigonometry honouring the angle mode; exact on quarter turns. */
static int trig_real(const rpn_ctx_t *c, int which, double d, double *out)
{
    double full, t, q, r;
    int k;

    if (c->angle == ANGLE_RAD) {
        r = d;
    } else {
        full = (c->angle == ANGLE_DEG) ? 360.0 : 400.0;
        t = fmod(d, full);              /* keeps the sign of d */
        q = full / 4.0;
        if (fmod(t, q) == 0.0) {        /* exact quarter turn */
            k = (int)(t / q);           /* -3 .. 3 */
            if (k < 0) {
                k += 4;
            }
            if (which == TR_SIN) {
                *out = quad_sin[k];
            } else if (which == TR_COS) {
                *out = quad_cos[k];
            } else if (k % 2 == 0) {
                *out = 0.0;
            } else {
                return RPN_E_DOMAIN;
            }
            return RPN_OK;
        }
        r = t * MF_PI / (full / 2.0);
    }
    if (which == TR_SIN) {
        *out = sin(r);
    } else if (which == TR_COS) {
        *out = cos(r);
    } else {
        *out = tan(r);
    }
    return RPN_OK;
}

static int trig(rpn_ctx_t *c, int which, cfn_t cf)
{
    rpn_value_t res;
    cx_t z;
    double out;
    int rc;

    if (!rpn_is_num(&STK_AT(c, 0))) {
        return RPN_E_TYPE;
    }
    if (STK_AT(c, 0).tag == RPN_T_COMPLEX) {
        rpn_to_cx(&STK_AT(c, 0), &z);
        rc = cf(&z, &z);
        if (rc == RPN_OK) {
            rc = rpn_ret_cx(&res, &z);
        }
    } else {
        rc = trig_real(c, which, rpn_to_d(&STK_AT(c, 0)), &out);
        if (rc == RPN_OK) {
            rc = rpn_ret_real(&res, out);
        }
    }
    if (rc == RPN_OK) {
        STK_AT(c, 0) = res;
    }
    return rc;
}

static int c_sin(rpn_ctx_t *c) { return trig(c, TR_SIN, cx_sin); }
static int c_cos(rpn_ctx_t *c) { return trig(c, TR_COS, cx_cos); }
static int c_tan(rpn_ctx_t *c) { return trig(c, TR_TAN, cx_tan); }

/* ---- two-real commands: atan2, polar, rect, complex -------------------- */

/* Both operands must be int or real. */
static int two_reals(rpn_ctx_t *c)
{
    int a = STK_AT(c, 0).tag;
    int b = STK_AT(c, 1).tag;

    if ((a != RPN_T_INT && a != RPN_T_REAL) ||
        (b != RPN_T_INT && b != RPN_T_REAL)) {
        return RPN_E_TYPE;
    }
    return RPN_OK;
}

/* Replaces y (below) and x (top) by one value. */
static int put2(rpn_ctx_t *c, const rpn_value_t *res)
{
    c->depth--;
    STK_AT(c, 0) = *res;
    return RPN_OK;
}

/* y x atan2 -> the angle of the point (x, y) in the angle mode. */
static int c_atan2(rpn_ctx_t *c)
{
    rpn_value_t res;
    int rc = two_reals(c);

    if (rc != RPN_OK) {
        return rc;
    }
    rc = rpn_ret_real(&res, rad_to_angle(c, atan2(rpn_to_d(&STK_AT(c, 1)),
                                                  rpn_to_d(&STK_AT(c, 0)))));
    return rc != RPN_OK ? rc : put2(c, &res);
}

/* y x polar -> theta r   (r on top, as on the HP-42S) */
static int c_polar(rpn_ctx_t *c)
{
    rpn_value_t r, th;
    double y, x;
    int rc = two_reals(c);

    if (rc != RPN_OK) {
        return rc;
    }
    y = rpn_to_d(&STK_AT(c, 1));
    x = rpn_to_d(&STK_AT(c, 0));
    rc = rpn_ret_real(&r, cx_hypot(x, y));
    if (rc == RPN_OK) {
        rc = rpn_ret_real(&th, rad_to_angle(c, atan2(y, x)));
    }
    if (rc != RPN_OK) {
        return rc;
    }
    STK_AT(c, 1) = th;
    STK_AT(c, 0) = r;
    return RPN_OK;
}

/* theta r rect -> y x   (x on top) */
static int c_rect(rpn_ctx_t *c)
{
    rpn_value_t x, y;
    double th, r, s, k;
    int rc = two_reals(c);

    if (rc != RPN_OK) {
        return rc;
    }
    th = rpn_to_d(&STK_AT(c, 1));
    r = rpn_to_d(&STK_AT(c, 0));
    rc = trig_real(c, TR_COS, th, &k);
    if (rc == RPN_OK) {
        rc = trig_real(c, TR_SIN, th, &s);
    }
    if (rc == RPN_OK) {
        rc = rpn_ret_real(&x, r * k);
    }
    if (rc == RPN_OK) {
        rc = rpn_ret_real(&y, r * s);
    }
    if (rc != RPN_OK) {
        return rc;
    }
    STK_AT(c, 1) = y;
    STK_AT(c, 0) = x;
    return RPN_OK;
}

/* re im complex -> (re,im) */
static int c_complex(rpn_ctx_t *c)
{
    rpn_value_t res;
    cx_t z;
    int rc = two_reals(c);

    if (rc != RPN_OK) {
        return rc;
    }
    cx_set(&z, rpn_to_d(&STK_AT(c, 1)), rpn_to_d(&STK_AT(c, 0)));
    rc = rpn_ret_cx(&res, &z);
    return rc != RPN_OK ? rc : put2(c, &res);
}

/* ---- complex helpers ---------------------------------------------------- */

static int c_re(rpn_ctx_t *c)
{
    rpn_value_t *x = &STK_AT(c, 0);

    if (!rpn_is_num(x)) {
        return RPN_E_TYPE;
    }
    if (x->tag == RPN_T_COMPLEX) {
        *x = rpn_value_real(x->u.c.re);
    }
    return RPN_OK;
}

static int c_im(rpn_ctx_t *c)
{
    rpn_value_t *x = &STK_AT(c, 0);

    if (!rpn_is_num(x)) {
        return RPN_E_TYPE;
    }
    if (x->tag == RPN_T_COMPLEX) {
        *x = rpn_value_real(x->u.c.im);
    } else if (x->tag == RPN_T_INT) {
        *x = rpn_value_int(0L);
    } else {
        *x = rpn_value_real(0.0);
    }
    return RPN_OK;
}

static int c_conj(rpn_ctx_t *c)
{
    rpn_value_t *x = &STK_AT(c, 0);

    if (!rpn_is_num(x)) {
        return RPN_E_TYPE;
    }
    if (x->tag == RPN_T_COMPLEX) {
        *x = rpn_value_complex(x->u.c.re, -x->u.c.im);
    }
    return RPN_OK;
}

/* The argument (phase) of a number, in the angle mode. */
static int c_arg(rpn_ctx_t *c)
{
    rpn_value_t res;
    cx_t z;
    int rc;

    if (!rpn_is_num(&STK_AT(c, 0))) {
        return RPN_E_TYPE;
    }
    rpn_to_cx(&STK_AT(c, 0), &z);
    rc = rpn_ret_real(&res, rad_to_angle(c, cx_arg(&z)));
    if (rc == RPN_OK) {
        STK_AT(c, 0) = res;
    }
    return rc;
}

/* A complex with a zero imaginary part becomes a real; others stay. */
static int c_demote(rpn_ctx_t *c)
{
    rpn_value_t *x = &STK_AT(c, 0);

    if (!rpn_is_num(x)) {
        return RPN_E_TYPE;
    }
    if (x->tag == RPN_T_COMPLEX && x->u.c.im == 0.0) {
        *x = rpn_value_real(x->u.c.re);
    }
    return RPN_OK;
}

/* ---- rounding ------------------------------------------------------------- */

/* An integral double becomes an int when it fits, else it stays real. */
static rpn_value_t int_or_real(double d)
{
    if (fabs(d) < (double)LONG_MAX) {
        return rpn_value_int((long)d);
    }
    return rpn_value_real(d);
}

static double r_trunc(double d)
{
    return (d < 0.0) ? ceil(d) : floor(d);
}

/* Half away from zero. */
static double r_round(double d)
{
    double t = r_trunc(d);

    if (fabs(d - t) >= 0.5) {
        t += (d < 0.0) ? -1.0 : 1.0;
    }
    return t;
}

static int rounding(rpn_ctx_t *c, rfn_t fn)
{
    rpn_value_t *x = &STK_AT(c, 0);

    if (x->tag == RPN_T_INT) {
        return RPN_OK;
    }
    if (x->tag != RPN_T_REAL) {
        return RPN_E_TYPE;
    }
    *x = int_or_real(fn(x->u.r));
    return RPN_OK;
}

static int c_floor(rpn_ctx_t *c) { return rounding(c, floor); }
static int c_ceil(rpn_ctx_t *c)  { return rounding(c, ceil); }
static int c_round(rpn_ctx_t *c) { return rounding(c, r_round); }
static int c_trunc(rpn_ctx_t *c) { return rounding(c, r_trunc); }

/* The fractional part, with the sign of the argument. */
static int c_fraction(rpn_ctx_t *c)
{
    rpn_value_t *x = &STK_AT(c, 0);

    if (x->tag == RPN_T_INT) {
        *x = rpn_value_int(0L);
        return RPN_OK;
    }
    if (x->tag != RPN_T_REAL) {
        return RPN_E_TYPE;
    }
    *x = rpn_value_real(x->u.r - r_trunc(x->u.r));
    return RPN_OK;
}

/* ---- constants -------------------------------------------------------------- */

static int c_pi(rpn_ctx_t *c)
{
    rpn_value_t v;

    v = rpn_value_real(MF_PI);
    return rpn_stk_push(c, &v);
}

static int c_e(rpn_ctx_t *c)
{
    rpn_value_t v;

    v = rpn_value_real(MF_E);
    return rpn_stk_push(c, &v);
}

/* ---- modes -------------------------------------------------------------------- */

static int c_radians(rpn_ctx_t *c) { c->angle = ANGLE_RAD; return RPN_OK; }
static int c_degrees(rpn_ctx_t *c) { c->angle = ANGLE_DEG; return RPN_OK; }
static int c_grads(rpn_ctx_t *c)   { c->angle = ANGLE_GRAD; return RPN_OK; }
static int c_all(rpn_ctx_t *c)     { c->disp = DISP_ALL; return RPN_OK; }
static int c_strict(rpn_ctx_t *c)  { c->strict = 1; return RPN_OK; }
static int c_lenient(rpn_ctx_t *c) { c->strict = 0; return RPN_OK; }

/* Pops the digit count n (an int, 0..DISP_DIGITS_MAX) and sets the mode. */
static int set_disp(rpn_ctx_t *c, int mode)
{
    long n;

    if (STK_AT(c, 0).tag != RPN_T_INT) {
        return RPN_E_TYPE;
    }
    n = STK_AT(c, 0).u.i;
    if (n < 0 || n > DISP_DIGITS_MAX) {
        return RPN_E_DOMAIN;
    }
    c->disp = mode;
    c->digits = (int)n;
    c->depth--;
    return RPN_OK;
}

static int c_fix(rpn_ctx_t *c) { return set_disp(c, DISP_FIX); }
static int c_sci(rpn_ctx_t *c) { return set_disp(c, DISP_SCI); }
static int c_eng(rpn_ctx_t *c) { return set_disp(c, DISP_ENG); }

/* ---- registry --------------------------------------------------------------------- */

const rpn_cmd_t rpn_math_cmds[] = {
    { "exp",     "e^x",    1, CMD_LASTX, c_exp      },
    { "ln",      NULL,     1, CMD_LASTX, c_ln       },
    { "log",     NULL,     1, CMD_LASTX, c_log      },
    { "log2",    NULL,     1, CMD_LASTX, c_log2     },
    { "ln1p",    "ln1+x",  1, CMD_LASTX, c_ln1p     },
    { "expm1",   "e^x-1",  1, CMD_LASTX, c_expm1    },
    { "pow10",   "10^x",   1, CMD_LASTX, c_pow10    },
    { "pow2",    NULL,     1, CMD_LASTX, c_pow2     },

    { "sin",     NULL,     1, CMD_LASTX, c_sin      },
    { "cos",     NULL,     1, CMD_LASTX, c_cos      },
    { "tan",     NULL,     1, CMD_LASTX, c_tan      },
    { "asin",    NULL,     1, CMD_LASTX, c_asin     },
    { "acos",    NULL,     1, CMD_LASTX, c_acos     },
    { "atan",    NULL,     1, CMD_LASTX, c_atan     },
    { "atan2",   NULL,     2, CMD_LASTX, c_atan2    },

    { "sinh",    NULL,     1, CMD_LASTX, c_sinh     },
    { "cosh",    NULL,     1, CMD_LASTX, c_cosh     },
    { "tanh",    NULL,     1, CMD_LASTX, c_tanh     },
    { "asinh",   NULL,     1, CMD_LASTX, c_asinh    },
    { "acosh",   NULL,     1, CMD_LASTX, c_acosh    },
    { "atanh",   NULL,     1, CMD_LASTX, c_atanh    },

    { "re",      NULL,     1, CMD_LASTX, c_re       },
    { "im",      NULL,     1, CMD_LASTX, c_im       },
    { "conj",    NULL,     1, CMD_LASTX, c_conj     },
    { "arg",     NULL,     1, CMD_LASTX, c_arg      },
    { "complex", NULL,     2, CMD_LASTX, c_complex  },
    { "polar",   "->pol",  2, CMD_LASTX, c_polar    },
    { "rect",    "->rec",  2, CMD_LASTX, c_rect     },
    { "demote",  NULL,     1, CMD_LASTX, c_demote   },

    { "floor",   NULL,     1, CMD_LASTX, c_floor    },
    { "ceil",    NULL,     1, CMD_LASTX, c_ceil     },
    { "round",   NULL,     1, CMD_LASTX, c_round    },
    { "trunc",   NULL,     1, CMD_LASTX, c_trunc    },
    { "fraction", "frac",  1, CMD_LASTX, c_fraction },

    { "pi",      NULL,     0, 0,         c_pi       },
    { "e",       NULL,     0, 0,         c_e        },

    { "radians", "rad",    0, 0,         c_radians  },
    { "degrees", "deg",    0, 0,         c_degrees  },
    { "grads",   "grad",   0, 0,         c_grads    },
    { "fix",     NULL,     1, 0,         c_fix      },
    { "sci",     NULL,     1, 0,         c_sci      },
    { "eng",     NULL,     1, 0,         c_eng      },
    { "all",     NULL,     0, 0,         c_all      },
    { "strict",  NULL,     0, 0,         c_strict   },
    { "lenient", NULL,     0, 0,         c_lenient  },

    { NULL,      NULL,     0, 0,         NULL       }
};
