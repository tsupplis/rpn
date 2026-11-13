/*
 * cond.c - tests (every name ends in '?') and flags.
 *
 * A test never changes the stack.  When it is false it sets c->skip, which
 * makes prog.c skip the next command (HP-42S rule); see prog.c.
 *
 *   x=0? x!=0? x<0? x>0? x<=0? x>=0?     compare the top with zero
 *   x=y? x!=y? x<y? x>y? x<=y? x>=y?     compare the top (x) with level 1 (y)
 *   real?      the top is an int or a real
 *   complex?   the top is complex
 *   integer?   the top is an int, or a real with no fractional part
 *   flag-set? { n }   flag-clear? { n }   test flag n (0..31)
 *   setflag { n }     clearflag { n }     change flag n
 *
 * Only = and != accept complex operands; ordering them is a type error.
 */
#include <math.h>
#include "intern.h"

#define OP_EQ 0
#define OP_NE 1
#define OP_LT 2
#define OP_GT 3
#define OP_LE 4
#define OP_GE 5

/* Compares a with b; *res receives the truth of "a op b". */
static int compare(int op, const rpn_value_t *a, const rpn_value_t *b,
                   int *res)
{
    double da, db;
    int cmp;
    cx_t za, zb;

    if (!rpn_is_num(a) || !rpn_is_num(b)) {
        return RPN_E_TYPE;
    }
    if (a->tag == RPN_T_COMPLEX || b->tag == RPN_T_COMPLEX) {
        if (op != OP_EQ && op != OP_NE) {
            return RPN_E_TYPE;
        }
        rpn_to_cx(a, &za);
        rpn_to_cx(b, &zb);
        cmp = (za.re == zb.re && za.im == zb.im);
        *res = (op == OP_EQ) ? cmp : !cmp;
        return RPN_OK;
    }
    if (a->tag == RPN_T_INT && b->tag == RPN_T_INT) {
        cmp = (a->u.i < b->u.i) ? -1 : (a->u.i > b->u.i);
    } else {
        da = rpn_to_d(a);
        db = rpn_to_d(b);
        cmp = (da < db) ? -1 : (da > db);
    }
    switch (op) {
    case OP_EQ: *res = (cmp == 0); break;
    case OP_NE: *res = (cmp != 0); break;
    case OP_LT: *res = (cmp < 0);  break;
    case OP_GT: *res = (cmp > 0);  break;
    case OP_LE: *res = (cmp <= 0); break;
    default:    *res = (cmp >= 0); break;
    }
    return RPN_OK;
}

static int finish(rpn_ctx_t *c, int result)
{
    c->skip = !result;
    return RPN_OK;
}

static int vs_zero(rpn_ctx_t *c, int op)
{
    rpn_value_t zero;
    int res = 0;
    int rc;

    zero = rpn_value_int(0L);
    rc = compare(op, &STK_AT(c, 0), &zero, &res);
    return rc != RPN_OK ? rc : finish(c, res);
}

static int vs_y(rpn_ctx_t *c, int op)
{
    int res = 0;
    int rc = compare(op, &STK_AT(c, 0), &STK_AT(c, 1), &res);

    return rc != RPN_OK ? rc : finish(c, res);
}

static int t_eq0(rpn_ctx_t *c) { return vs_zero(c, OP_EQ); }
static int t_ne0(rpn_ctx_t *c) { return vs_zero(c, OP_NE); }
static int t_lt0(rpn_ctx_t *c) { return vs_zero(c, OP_LT); }
static int t_gt0(rpn_ctx_t *c) { return vs_zero(c, OP_GT); }
static int t_le0(rpn_ctx_t *c) { return vs_zero(c, OP_LE); }
static int t_ge0(rpn_ctx_t *c) { return vs_zero(c, OP_GE); }
static int t_eqy(rpn_ctx_t *c) { return vs_y(c, OP_EQ); }
static int t_ney(rpn_ctx_t *c) { return vs_y(c, OP_NE); }
static int t_lty(rpn_ctx_t *c) { return vs_y(c, OP_LT); }
static int t_gty(rpn_ctx_t *c) { return vs_y(c, OP_GT); }
static int t_ley(rpn_ctx_t *c) { return vs_y(c, OP_LE); }
static int t_gey(rpn_ctx_t *c) { return vs_y(c, OP_GE); }

static int t_real(rpn_ctx_t *c)
{
    int tag = STK_AT(c, 0).tag;

    return finish(c, tag == RPN_T_INT || tag == RPN_T_REAL);
}

static int t_complex(rpn_ctx_t *c)
{
    return finish(c, STK_AT(c, 0).tag == RPN_T_COMPLEX);
}

static int t_integer(rpn_ctx_t *c)
{
    const rpn_value_t *x = &STK_AT(c, 0);
    double ip;

    if (x->tag == RPN_T_INT) {
        return finish(c, 1);
    }
    if (x->tag == RPN_T_REAL) {
        return finish(c, modf(x->u.r, &ip) == 0.0);
    }
    return finish(c, 0);
}

/* ---- flags ---------------------------------------------------------------- */

static int t_flag_set(rpn_ctx_t *c)
{
    return finish(c, (int)((c->flags >> c->argn) & 1UL));
}

static int t_flag_clear(rpn_ctx_t *c)
{
    return finish(c, !((c->flags >> c->argn) & 1UL));
}

static int c_setflag(rpn_ctx_t *c)
{
    c->flags |= 1UL << c->argn;
    return RPN_OK;
}

static int c_clearflag(rpn_ctx_t *c)
{
    c->flags &= ~(1UL << c->argn);
    return RPN_OK;
}

const rpn_cmd_t rpn_cond_cmds[] = {
    { "x=0?",  NULL, 1, 0, t_eq0 },
    { "x!=0?", NULL, 1, 0, t_ne0 },
    { "x<0?",  NULL, 1, 0, t_lt0 },
    { "x>0?",  NULL, 1, 0, t_gt0 },
    { "x<=0?", NULL, 1, 0, t_le0 },
    { "x>=0?", NULL, 1, 0, t_ge0 },
    { "x=y?",  NULL, 2, 0, t_eqy },
    { "x!=y?", NULL, 2, 0, t_ney },
    { "x<y?",  NULL, 2, 0, t_lty },
    { "x>y?",  NULL, 2, 0, t_gty },
    { "x<=y?", NULL, 2, 0, t_ley },
    { "x>=y?", NULL, 2, 0, t_gey },
    { "real?",    NULL, 1, 0, t_real    },
    { "complex?", NULL, 1, 0, t_complex },
    { "integer?", NULL, 1, 0, t_integer },
    { "flag-set?",   "fs?", 0, CMD_ARG_FLAG, t_flag_set   },
    { "flag-clear?", "fc?", 0, CMD_ARG_FLAG, t_flag_clear },
    { "setflag",   "sf", 0, CMD_ARG_FLAG, c_setflag   },
    { "clearflag", "cf", 0, CMD_ARG_FLAG, c_clearflag },
    { NULL, NULL, 0, 0, NULL }
};
