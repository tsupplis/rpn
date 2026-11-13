/*
 * stack.c - stack storage, the public stack API and the stack commands.
 *
 * The stack is a growable array: stack[depth-1] is level 0 (the top).
 * Growth is bounded by the configured maximum.  There is no automatic
 * duplication of the top register: dropping from a stack of depth 1 leaves
 * it empty, and commands that need more values than exist fail with
 * RPN_E_STACK_UNDERFLOW.
 */
#include <string.h>
#include "intern.h"

/* Makes room for one more entry.  The stack is unchanged on failure. */
static int grow(rpn_ctx_t *c)
{
    size_t ncap;
    rpn_value_t *ns;

    if (c->depth < c->cap) {
        return RPN_OK;
    }
    ncap = c->cap ? c->cap * 2 : 8;
    if (ncap > c->max) {
        ncap = c->max;
    }
    ns = (rpn_value_t *)c->env.alloc(c->env.user, ncap * sizeof *ns);
    if (ns == NULL) {
        return RPN_E_NOMEM;
    }
    if (c->depth > 0) {
        memcpy(ns, c->stack, c->depth * sizeof *ns);
    }
    if (c->stack != NULL) {
        c->env.release(c->env.user, c->stack);
    }
    c->stack = ns;
    c->cap = ncap;
    return RPN_OK;
}

int rpn_stk_push(rpn_ctx_t *c, const rpn_value_t *v)
{
    int rc;

    if (c->depth >= c->max) {
        return RPN_E_STACK_OVERFLOW;
    }
    rc = grow(c);
    if (rc != RPN_OK) {
        return rc;
    }
    c->stack[c->depth++] = *v;
    return RPN_OK;
}

/* ---- public API -------------------------------------------------- */

int rpn_set_stack_max(rpn_ctx_t *c, size_t max)
{
    if (max < 1 || max < c->depth) {
        return RPN_E_BAD_ARG;
    }
    c->max = max;
    return RPN_OK;
}

size_t rpn_stack_max(const rpn_ctx_t *c)
{
    return c->max;
}

size_t rpn_depth(const rpn_ctx_t *c)
{
    return c->depth;
}

int rpn_peek(const rpn_ctx_t *c, size_t level, rpn_value_t *out)
{
    if (level >= c->depth) {
        return RPN_E_STACK_UNDERFLOW;
    }
    *out = STK_AT(c, level);
    return RPN_OK;
}

int rpn_push(rpn_ctx_t *c, const rpn_value_t *v)
{
    if (v->tag != RPN_T_INT && v->tag != RPN_T_REAL &&
        v->tag != RPN_T_COMPLEX) {
        return RPN_E_TYPE;
    }
    if (v->tag == RPN_T_REAL && !RPN_FINITE(v->u.r)) {
        return RPN_E_REAL_OVERFLOW;
    }
    if (v->tag == RPN_T_COMPLEX &&
        (!RPN_FINITE(v->u.c.re) || !RPN_FINITE(v->u.c.im))) {
        return RPN_E_REAL_OVERFLOW;
    }
    return rpn_stk_push(c, v);
}

/* ---- commands ---------------------------------------------------- */

static int c_dup(rpn_ctx_t *c)
{
    rpn_value_t top;

    top = STK_AT(c, 0);
    return rpn_stk_push(c, &top);
}

static int c_drop(rpn_ctx_t *c)
{
    c->depth--;
    return RPN_OK;
}

static int c_swap(rpn_ctx_t *c)
{
    rpn_value_t t;

    t = STK_AT(c, 0);
    STK_AT(c, 0) = STK_AT(c, 1);
    STK_AT(c, 1) = t;
    return RPN_OK;
}

/* The top value moves to the bottom; everything else moves up. */
static int c_rolldown(rpn_ctx_t *c)
{
    rpn_value_t top;

    top = c->stack[c->depth - 1];
    memmove(c->stack + 1, c->stack, (c->depth - 1) * sizeof *c->stack);
    c->stack[0] = top;
    return RPN_OK;
}

/* The bottom value moves to the top; everything else moves down. */
static int c_rollup(rpn_ctx_t *c)
{
    rpn_value_t bottom;

    bottom = c->stack[0];
    memmove(c->stack, c->stack + 1, (c->depth - 1) * sizeof *c->stack);
    c->stack[c->depth - 1] = bottom;
    return RPN_OK;
}

static int c_clear(rpn_ctx_t *c)
{
    c->depth = 0;
    return RPN_OK;
}

static int c_lastx(rpn_ctx_t *c)
{
    rpn_value_t v;

    v = c->lastx;
    return rpn_stk_push(c, &v);
}

const rpn_cmd_t rpn_stack_cmds[] = {
    { "dup",      "enter", 1, 0, c_dup     },
    { "drop",     NULL,   1, 0, c_drop     },
    { "swap",     "x<>y", 2, 0, c_swap     },
    { "rolldown", "rdn",  2, 0, c_rolldown },
    { "rollup",   "rup",  2, 0, c_rollup   },
    { "clear",    "clx",  0, 0, c_clear    },
    { "lastx",    NULL,   0, 0, c_lastx    },
    { NULL,       NULL,   0, 0, NULL       }
};
