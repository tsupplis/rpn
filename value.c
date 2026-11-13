/*
 * value.c - value constructors, number parsing and number formatting.
 *
 * Literal grammar (after lower-casing):
 *     int     :  [+-] digits
 *     real    :  [+-] ( digits [ "." [digits] ] | "." digits ) [ "e" [+-] digits ]
 *                where a "." or an exponent makes the literal real.
 *     complex :  "(" scalar "," scalar ")"   no spaces, each scalar an int
 *                or a real; the value is always complex.
 * A lone "+", "-", "." or "e" is not a number.
 *
 * Display modes (reals and the parts of complex values; ints are never
 * affected):
 *     all   %.12g, with ".0" added when the text has no '.' or 'e'
 *     fix n n digits after the point; |x| >= 1e15 falls back to sci
 *     sci n n digits after the point, exponent form  d.ddde+xx
 *     eng n n digits after the point, exponent a multiple of 3
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "intern.h"

rpn_value_t rpn_value_int(long i)
{
    rpn_value_t v;

    memset(&v, 0, sizeof v);
    v.tag = RPN_T_INT;
    v.u.i = i;
    return v;
}

rpn_value_t rpn_value_real(double r)
{
    rpn_value_t v;

    memset(&v, 0, sizeof v);
    v.tag = RPN_T_REAL;
    v.u.r = r;
    return v;
}

rpn_value_t rpn_value_complex(double re, double im)
{
    rpn_value_t v;

    memset(&v, 0, sizeof v);
    v.tag = RPN_T_COMPLEX;
    v.u.c.re = re;
    v.u.c.im = im;
    return v;
}

/* ---- parsing ------------------------------------------------------- */

/* Parses the digits of an integer literal. Returns 0 on overflow. */
static int parse_int(const char *p, int neg, long *out)
{
    unsigned long acc = 0;
    unsigned long limit = neg ? (unsigned long)LONG_MAX + 1UL
                              : (unsigned long)LONG_MAX;
    unsigned long d;

    for (; isdigit((unsigned char)*p); p++) {
        d = (unsigned long)(*p - '0');
        if (acc > (limit - d) / 10UL) {
            return 0;
        }
        acc = acc * 10UL + d;
    }
    if (acc == 0UL) {
        *out = 0L;
    } else if (neg) {
        *out = -(long)(acc - 1UL) - 1L;
    } else {
        *out = (long)acc;
    }
    return 1;
}

/* Parses an int or real literal (not a complex one). */
static int scalar_parse(const char *s, rpn_value_t *out, int *err)
{
    const char *p = s;
    const char *q;
    int neg = 0;
    int is_real = 0;
    size_t ni, nf = 0;
    long iv;
    double rv;

    if (*p == '+' || *p == '-') {
        neg = (*p == '-');
        p++;
    }
    q = p;
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    ni = (size_t)(p - q);
    if (*p == '.') {
        is_real = 1;
        p++;
        q = p;
        while (isdigit((unsigned char)*p)) {
            p++;
        }
        nf = (size_t)(p - q);
    }
    if (ni + nf == 0) {
        return 0;
    }
    if (*p == 'e') {
        p++;
        if (*p == '+' || *p == '-') {
            p++;
        }
        if (!isdigit((unsigned char)*p)) {
            return 0;
        }
        while (isdigit((unsigned char)*p)) {
            p++;
        }
        is_real = 1;
    }
    if (*p != '\0') {
        return 0;
    }

    if (!is_real) {
        const char *digits = s;

        if (*digits == '+' || *digits == '-') {
            digits++;
        }
        if (!parse_int(digits, neg, &iv)) {
            *err = RPN_E_INT_OVERFLOW;
            return -1;
        }
        *out = rpn_value_int(iv);
        return 1;
    }
    rv = strtod(s, NULL);
    if (!RPN_FINITE(rv)) {
        *err = RPN_E_REAL_OVERFLOW;
        return -1;
    }
    *out = rpn_value_real(rv);
    return 1;
}

static double scalar_d(const rpn_value_t *v)
{
    return v->tag == RPN_T_INT ? (double)v->u.i : v->u.r;
}

int rpn_num_parse(const char *s, rpn_value_t *out, int *err)
{
    char part[RPN_MAX_TOKEN + 1];
    const char *comma;
    size_t n, k;
    rpn_value_t re, im;
    int r1, r2;

    if (s[0] != '(') {
        return scalar_parse(s, out, err);
    }
    n = strlen(s);
    comma = strchr(s, ',');
    if (n < 5 || s[n - 1] != ')' || !comma) {
        return 0;
    }
    k = (size_t)(comma - s) - 1;          /* length of the real part */
    memcpy(part, s + 1, k);
    part[k] = '\0';
    r1 = scalar_parse(part, &re, err);
    if (r1 == 0) {
        return 0;
    }
    k = n - 1 - (size_t)(comma - s) - 1;  /* length of the imaginary part */
    memcpy(part, comma + 1, k);
    part[k] = '\0';
    r2 = scalar_parse(part, &im, err);
    if (r2 == 0) {
        return 0;
    }
    if (r1 < 0 || r2 < 0) {
        return -1;      /* *err was set by scalar_parse */
    }
    *out = rpn_value_complex(scalar_d(&re), scalar_d(&im));
    return 1;
}

int rpn_parse_number(const char *token, rpn_value_t *out)
{
    int err = RPN_OK;

    return rpn_num_parse(token, out, &err);
}

/* ---- formatting ---------------------------------------------------- */

/*
 * Formats a real as the "all" mode does: up to 12 significant digits,
 * without trailing zeros.  The zeros are removed here because some C
 * libraries (Aztec C) do not do it for %g.
 */
static void fmt_all(char *buf, double x, int force_point)
{
    char *e, *end;
    char tail[16];

    sprintf(buf, "%.12g", x);
    e = strchr(buf, 'e');
    tail[0] = '\0';
    if (e != NULL) {
        strcpy(tail, e);
        *e = '\0';
    }
    if (strchr(buf, '.') != NULL) {
        end = buf + strlen(buf);
        while (end[-1] == '0') {
            *--end = '\0';
        }
        if (end[-1] == '.') {
            *--end = '\0';
        }
    }
    strcat(buf, tail);
    if (force_point && strpbrk(buf, ".e") == NULL) {
        strcat(buf, ".0");
    }
}

static void fmt_sci(char *buf, double x, int digits)
{
    sprintf(buf, "%.*e", digits, x);
}

/*
 * Engineering notation: the exponent is a multiple of 3 and the mantissa
 * lies in [1, 1000) (0 prints as 0.00e0).  Rounding can carry a mantissa
 * of 999.99.. up to 1000, which moves it to the next exponent.
 */
static void fmt_eng(char *buf, double x, int digits)
{
    int e, e3 = 0;
    double ax = fabs(x);

    if (ax != 0.0) {
        e = (int)floor(log10(ax));
        e3 = (e >= 0) ? (e / 3) * 3 : -((-e + 2) / 3) * 3;
        if (ax / pow(10.0, (double)e3) < 1.0) {
            e3 -= 3;
        }
        sprintf(buf, "%.*f", digits, x / pow(10.0, (double)e3));
        if (fabs(atof(buf)) >= 1000.0) {
            e3 += 3;
            sprintf(buf, "%.*f", digits, x / pow(10.0, (double)e3));
        }
    } else {
        sprintf(buf, "%.*f", digits, 0.0);
    }
    sprintf(buf + strlen(buf), "e%d", e3);
}

static void fmt_real(const rpn_ctx_t *c, char *buf, double x)
{
    int mode = c ? c->disp : DISP_ALL;
    int digits = c ? c->digits : 0;

    if (mode == DISP_FIX && fabs(x) < 1e15) {
        sprintf(buf, "%.*f", digits, x);
        if (strchr(buf, '.') == NULL) {
            strcat(buf, ".");
        }
    } else if (mode == DISP_FIX || mode == DISP_SCI) {
        fmt_sci(buf, x, digits);
    } else if (mode == DISP_ENG) {
        fmt_eng(buf, x, digits);
    } else {
        fmt_all(buf, x, 1);
    }
}

int rpn_format(const rpn_ctx_t *c, const rpn_value_t *v, char *buf,
               size_t bufsz)
{
    char re[48];
    char im[48];

    if (bufsz < RPN_FORMAT_MAX) {
        return -1;
    }
    if (v->tag == RPN_T_INT) {
        sprintf(buf, "%ld", v->u.i);
        return 0;
    }
    if (v->tag == RPN_T_REAL) {
        fmt_real(c, buf, v->u.r);
        return 0;
    }
    if (v->tag == RPN_T_COMPLEX) {
        fmt_real(c, re, v->u.c.re);
        fmt_real(c, im, v->u.c.im);
        sprintf(buf, "(%s,%s)", re, im);
        return 0;
    }
    return -1;
}
