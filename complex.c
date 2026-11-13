/*
 * complex.c - complex arithmetic helpers (no <complex.h>; C89 only).
 *
 * Every function takes its result as the first argument, and that result
 * may be the same object as an operand.  Functions that can fail return
 * RPN_OK or an error code (RPN_E_DIV_ZERO, RPN_E_DOMAIN,
 * RPN_E_REAL_OVERFLOW); on failure *r is unspecified.  All functions use
 * the principal branch, with the usual cuts: log and sqrt along the
 * negative real axis, asin/acos along the real axis outside [-1, 1],
 * atan along the imaginary axis outside [-i, i].  An argument whose
 * imaginary part is zero is treated as lying just above the cut (+0i), as
 * C99 does, so asin(2) = (1.5708, +1.3170).
 */
#include <math.h>
#include "intern.h"

#define CX_PI 3.14159265358979323846
#define CX_HALF_PI 1.57079632679489661923
#define CX_LN2 0.69314718055994530942

/* ---- value helpers shared with the command modules ----------------- */

int rpn_is_num(const rpn_value_t *v)
{
    return v->tag == RPN_T_INT || v->tag == RPN_T_REAL ||
           v->tag == RPN_T_COMPLEX;
}

/* Value of an int or real (not for complex). */
double rpn_to_d(const rpn_value_t *v)
{
    return v->tag == RPN_T_INT ? (double)v->u.i : v->u.r;
}

/* Any number as a complex (reals and ints get a zero imaginary part). */
void rpn_to_cx(const rpn_value_t *v, cx_t *z)
{
    if (v->tag == RPN_T_COMPLEX) {
        z->re = v->u.c.re;
        z->im = v->u.c.im;
    } else {
        z->re = rpn_to_d(v);
        z->im = 0.0;
    }
}

/* Stores a real result, rejecting NaN (domain) and infinity (overflow). */
int rpn_ret_real(rpn_value_t *out, double r)
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

/* Stores a complex result, rejecting non-finite parts. */
int rpn_ret_cx(rpn_value_t *out, const cx_t *z)
{
    if (RPN_ISINF(z->re) || RPN_ISINF(z->im)) {
        return RPN_E_REAL_OVERFLOW;
    }
    if (RPN_ISNAN(z->re) || RPN_ISNAN(z->im)) {
        return RPN_E_DOMAIN;
    }
    *out = rpn_value_complex(z->re, z->im);
    return RPN_OK;
}

/* ---- construction and checks -------------------------------------- */

void cx_set(cx_t *r, double re, double im)
{
    r->re = re;
    r->im = im;
}

/* Maps a non-finite result to an error code (overflow wins over NaN,
 * because inf * 0 produces a NaN that is really an overflow). */
static int cx_check(const cx_t *r)
{
    if (RPN_ISINF(r->re) || RPN_ISINF(r->im)) {
        return RPN_E_REAL_OVERFLOW;
    }
    if (RPN_ISNAN(r->re) || RPN_ISNAN(r->im)) {
        return RPN_E_DOMAIN;
    }
    return RPN_OK;
}

/* sqrt(a*a + b*b) without intermediate overflow. */
double cx_hypot(double a, double b)
{
    double t;

    a = fabs(a);
    b = fabs(b);
    if (a < b) {
        t = a;
        a = b;
        b = t;
    }
    if (b == 0.0) {
        return a;               /* exact, whatever the quality of sqrt */
    }
    t = b / a;
    return a * sqrt(1.0 + t * t);
}

double cx_abs(const cx_t *z)
{
    return cx_hypot(z->re, z->im);
}

double cx_arg(const cx_t *z)
{
    return atan2(z->im, z->re);
}

/* ---- field operations --------------------------------------------- */

int cx_add(cx_t *r, const cx_t *a, const cx_t *b)
{
    double re = a->re + b->re;
    double im = a->im + b->im;

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_sub(cx_t *r, const cx_t *a, const cx_t *b)
{
    double re = a->re - b->re;
    double im = a->im - b->im;

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_mul(cx_t *r, const cx_t *a, const cx_t *b)
{
    double re = a->re * b->re - a->im * b->im;
    double im = a->re * b->im + a->im * b->re;

    cx_set(r, re, im);
    return cx_check(r);
}

/* Smith's algorithm: no overflow in the intermediate products. */
int cx_div(cx_t *r, const cx_t *a, const cx_t *b)
{
    double q, d, re, im;

    if (b->re == 0.0 && b->im == 0.0) {
        return RPN_E_DIV_ZERO;
    }
    if (fabs(b->re) >= fabs(b->im)) {
        q = b->im / b->re;
        d = b->re + b->im * q;
        re = (a->re + a->im * q) / d;
        im = (a->im - a->re * q) / d;
    } else {
        q = b->re / b->im;
        d = b->re * q + b->im;
        re = (a->re * q + a->im) / d;
        im = (a->im * q - a->re) / d;
    }
    cx_set(r, re, im);
    return cx_check(r);
}

/* ---- exponential, logarithm, roots, powers ------------------------ */

int cx_exp(cx_t *r, const cx_t *z)
{
    double m = exp(z->re);
    double re = m * cos(z->im);
    double im = m * sin(z->im);

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_log(cx_t *r, const cx_t *z)
{
    double re, im;

    if (z->re == 0.0 && z->im == 0.0) {
        return RPN_E_DOMAIN;
    }
    re = log(cx_abs(z));
    im = cx_arg(z);
    cx_set(r, re, im);
    return cx_check(r);
}

int cx_sqrt(cx_t *r, const cx_t *z)
{
    double m, t, re, im;

    m = cx_abs(z);
    if (m == 0.0) {
        cx_set(r, 0.0, 0.0);
        return RPN_OK;
    }
    if (z->re >= 0.0) {
        t = sqrt((m + z->re) / 2.0);
        re = t;
        im = z->im / (2.0 * t);
    } else {
        t = sqrt((m - z->re) / 2.0);
        re = fabs(z->im) / (2.0 * t);
        im = (z->im >= 0.0) ? t : -t;
    }
    cx_set(r, re, im);
    return cx_check(r);
}

/* a ** b = exp(b * log a); 0 ** b is 0 for Re(b) > 0, else a domain error. */
int cx_pow(cx_t *r, const cx_t *a, const cx_t *b)
{
    cx_t l;
    int rc;

    if (a->re == 0.0 && a->im == 0.0) {
        if (b->re > 0.0) {
            cx_set(r, 0.0, 0.0);
            return RPN_OK;
        }
        if (b->re == 0.0 && b->im == 0.0) {
            cx_set(r, 1.0, 0.0);
            return RPN_OK;
        }
        return RPN_E_DIV_ZERO;
    }
    rc = cx_log(&l, a);
    if (rc != RPN_OK) {
        return rc;
    }
    rc = cx_mul(&l, &l, b);
    if (rc != RPN_OK) {
        return rc;
    }
    return cx_exp(r, &l);
}

/* ---- trigonometric ------------------------------------------------- */

int cx_sin(cx_t *r, const cx_t *z)
{
    double re = sin(z->re) * cosh(z->im);
    double im = cos(z->re) * sinh(z->im);

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_cos(cx_t *r, const cx_t *z)
{
    double re = cos(z->re) * cosh(z->im);
    double im = -sin(z->re) * sinh(z->im);

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_tan(cx_t *r, const cx_t *z)
{
    cx_t s, c;
    int rc;

    rc = cx_sin(&s, z);
    if (rc == RPN_OK) {
        rc = cx_cos(&c, z);
    }
    if (rc == RPN_OK) {
        rc = cx_div(r, &s, &c);
    }
    return rc;
}

/* ---- hyperbolic ---------------------------------------------------- */

int cx_sinh(cx_t *r, const cx_t *z)
{
    double re = sinh(z->re) * cos(z->im);
    double im = cosh(z->re) * sin(z->im);

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_cosh(cx_t *r, const cx_t *z)
{
    double re = cosh(z->re) * cos(z->im);
    double im = sinh(z->re) * sin(z->im);

    cx_set(r, re, im);
    return cx_check(r);
}

int cx_tanh(cx_t *r, const cx_t *z)
{
    cx_t s, c;
    int rc;

    rc = cx_sinh(&s, z);
    if (rc == RPN_OK) {
        rc = cx_cosh(&c, z);
    }
    if (rc == RPN_OK) {
        rc = cx_div(r, &s, &c);
    }
    return rc;
}

/* ---- inverse hyperbolic -------------------------------------------- */

/* acosh of a real x >= 1. */
static double acosh_pos(double x)
{
    if (x > 1e8) {
        return log(x) + CX_LN2;
    }
    return log(x + sqrt((x - 1.0) * (x + 1.0)));
}

/* asinh z = log(z + sqrt(z*z + 1)); odd, so work on Re z >= 0. */
int cx_asinh(cx_t *r, const cx_t *z)
{
    cx_t t, one, w;
    int rc;

    if (z->re < 0.0) {
        cx_set(&w, -z->re, -z->im);
        rc = cx_asinh(r, &w);
        if (rc == RPN_OK) {
            cx_set(r, -r->re, -r->im);
        }
        return rc;
    }
    cx_set(&one, 1.0, 0.0);
    rc = cx_mul(&t, z, z);
    if (rc == RPN_OK) {
        rc = cx_add(&t, &t, &one);
    }
    if (rc == RPN_OK) {
        rc = cx_sqrt(&t, &t);
    }
    if (rc == RPN_OK) {
        rc = cx_add(&t, &t, z);
    }
    if (rc == RPN_OK) {
        rc = cx_log(r, &t);
    }
    return rc;
}

/* acosh z = log(z + sqrt(z + 1) * sqrt(z - 1)); real axis done directly. */
int cx_acosh(cx_t *r, const cx_t *z)
{
    cx_t p, m, one;
    double re = z->re;
    int rc;

    if (z->im == 0.0) {
        if (re >= 1.0) {
            cx_set(r, acosh_pos(re), 0.0);
        } else if (re >= -1.0) {
            cx_set(r, 0.0, acos(re));
        } else {
            cx_set(r, acosh_pos(-re), CX_PI);
        }
        return RPN_OK;
    }
    cx_set(&one, 1.0, 0.0);
    rc = cx_add(&p, z, &one);
    if (rc == RPN_OK) {
        rc = cx_sub(&m, z, &one);
    }
    if (rc == RPN_OK) {
        rc = cx_sqrt(&p, &p);
    }
    if (rc == RPN_OK) {
        rc = cx_sqrt(&m, &m);
    }
    if (rc == RPN_OK) {
        rc = cx_mul(&p, &p, &m);
    }
    if (rc == RPN_OK) {
        rc = cx_add(&p, &p, z);
    }
    if (rc == RPN_OK) {
        rc = cx_log(r, &p);
    }
    return rc;
}

/* atanh z = (log(1 + z) - log(1 - z)) / 2; real axis done directly. */
int cx_atanh(cx_t *r, const cx_t *z)
{
    cx_t p, m, one;
    double re = z->re;
    int rc;

    if (z->im == 0.0) {
        if (fabs(re) == 1.0) {
            return RPN_E_DOMAIN;
        }
        if (fabs(re) < 1.0) {
            cx_set(r, 0.5 * log((1.0 + re) / (1.0 - re)), 0.0);
        } else {
            cx_set(r, 0.5 * log(fabs((1.0 + re) / (1.0 - re))), CX_HALF_PI);
        }
        return RPN_OK;
    }
    cx_set(&one, 1.0, 0.0);
    rc = cx_add(&p, &one, z);
    if (rc == RPN_OK) {
        rc = cx_sub(&m, &one, z);
    }
    if (rc == RPN_OK) {
        rc = cx_log(&p, &p);
    }
    if (rc == RPN_OK) {
        rc = cx_log(&m, &m);
    }
    if (rc == RPN_OK) {
        rc = cx_sub(r, &p, &m);
    }
    if (rc == RPN_OK) {
        cx_set(r, r->re / 2.0, r->im / 2.0);
    }
    return rc;
}

/* ---- inverse trigonometric (through the hyperbolic ones) ----------- */

/* asin z = -i * asinh(i z); real axis beyond +-1 done directly. */
int cx_asin(cx_t *r, const cx_t *z)
{
    cx_t iz;
    int rc;

    if (z->im == 0.0 && fabs(z->re) > 1.0) {
        cx_set(r, (z->re > 0.0) ? CX_HALF_PI : -CX_HALF_PI,
               acosh_pos(fabs(z->re)));
        return RPN_OK;
    }
    cx_set(&iz, -z->im, z->re);
    rc = cx_asinh(&iz, &iz);
    if (rc == RPN_OK) {
        cx_set(r, iz.im, -iz.re);
    }
    return rc;
}

/* acos z = pi/2 - asin z; real axis beyond +-1 done directly. */
int cx_acos(cx_t *r, const cx_t *z)
{
    cx_t a;
    int rc;

    if (z->im == 0.0 && fabs(z->re) > 1.0) {
        cx_set(r, (z->re > 0.0) ? 0.0 : CX_PI, -acosh_pos(fabs(z->re)));
        return RPN_OK;
    }
    rc = cx_asin(&a, z);
    if (rc == RPN_OK) {
        cx_set(r, CX_HALF_PI - a.re, -a.im);
    }
    return rc;
}

/* atan z = -i * atanh(i z) */
int cx_atan(cx_t *r, const cx_t *z)
{
    cx_t iz;
    int rc;

    cx_set(&iz, -z->im, z->re);
    rc = cx_atanh(&iz, &iz);
    if (rc == RPN_OK) {
        cx_set(r, iz.im, -iz.re);
    }
    return rc;
}
