/*
 * vars.c - global variables, storage arithmetic and the loop helpers.
 *
 *   store { x }   pop the top of the stack into x (use dup first to keep
 *                 a copy: the stack is bounded, so a store that kept its
 *                 value would leak one entry per use in a loop)
 *   recall { x }  push the value of x
 *   store+ { x }  x = x + top, popping the top  (also store- store* store/)
 *   clearvars     delete every variable
 *   view { x }    print "x = value" through the environment's out hook
 *   decrement-skip-zero { x }, increment-skip-greater { x }
 *                 the HP-42S loop counters (see below)
 *
 * A variable name is [a-z][a-z0-9_]* and may not be a command word.  A bare
 * name in the input pushes the variable's value (see prog.c).
 *
 * Loop counters use the HP-42S format ccc.fffii: ccc is the counter, fff
 * the final value and ii the step (0 means 1).  An int variable is a
 * counter with final value 0 and step 1.
 *   decrement-skip-zero: ccc = ccc - ii; skip the next command if ccc <= fff
 *   increment-skip-greater: ccc = ccc + ii; skip the next command if ccc > fff
 */
#include <math.h>
#include <string.h>
#include "intern.h"

/* ---- storage ------------------------------------------------------- */

/* The locals of the running function shadow the globals. */
static var_t *find_local(const rpn_ctx_t *c, const char *name)
{
    size_t i;

    for (i = c->nlocs; i > c->lbase; i--) {
        if (strcmp(c->locs[i - 1].name, name) == 0) {
            return &c->locs[i - 1];
        }
    }
    return NULL;
}

static var_t *find_var(const rpn_ctx_t *c, const char *name)
{
    size_t i;
    var_t *l = find_local(c, name);

    if (l) {
        return l;
    }
    for (i = 0; i < c->nvars; i++) {
        if (strcmp(c->vars[i].name, name) == 0) {
            return &c->vars[i];
        }
    }
    return NULL;
}

int rpn_local_def(rpn_ctx_t *c, const char *name, const rpn_value_t *val)
{
    var_t *l = find_local(c, name);
    var_t *nl;

    if (l) {
        l->v = *val;
        return RPN_OK;
    }
    nl = (var_t *)rpn_grow(c, c->locs, &c->locap, sizeof(var_t),
                           c->nlocs + 1, c->nlocs);
    if (nl == NULL) {
        return RPN_E_NOMEM;
    }
    c->locs = nl;
    l = &c->locs[c->nlocs++];
    strcpy(l->name, name);
    l->v = *val;
    return RPN_OK;
}

const rpn_value_t *rpn_var_lookup(const rpn_ctx_t *c, const char *name)
{
    var_t *v = find_var(c, name);

    return v ? &v->v : NULL;
}

int rpn_var_set(rpn_ctx_t *c, const char *name, const rpn_value_t *val)
{
    var_t *v = find_var(c, name);
    var_t *nv;

    if (v) {
        v->v = *val;
        return RPN_OK;
    }
    if (rpn_func_find(c, name, NULL)) {
        return RPN_E_BAD_NAME;
    }
    nv = (var_t *)rpn_grow(c, c->vars, &c->vcap, sizeof(var_t),
                           c->nvars + 1, c->nvars);
    if (nv == NULL) {
        return RPN_E_NOMEM;
    }
    c->vars = nv;
    v = &c->vars[c->nvars];
    strcpy(v->name, name);
    v->v = *val;
    c->nvars++;
    return RPN_OK;
}

/* ---- commands --------------------------------------------------------- */

static int c_store(rpn_ctx_t *c)
{
    int rc = rpn_var_set(c, c->arg, &STK_AT(c, 0));

    if (rc == RPN_OK) {
        c->depth--;
    }
    return rc;
}

static int c_recall(rpn_ctx_t *c)
{
    const rpn_value_t *v = rpn_var_lookup(c, c->arg);

    if (!v) {
        return RPN_E_UNDEFINED;
    }
    return rpn_stk_push(c, v);
}

static int store_op(rpn_ctx_t *c, int op)
{
    const rpn_value_t *cur = rpn_var_lookup(c, c->arg);
    rpn_value_t res;
    int rc;

    if (!cur) {
        return RPN_E_UNDEFINED;
    }
    rc = rpn_binary_op(op, cur, &STK_AT(c, 0), &res);
    if (rc != RPN_OK) {
        return rc;
    }
    rc = rpn_var_set(c, c->arg, &res);
    if (rc == RPN_OK) {
        c->depth--;
    }
    return rc;
}

static int c_store_add(rpn_ctx_t *c) { return store_op(c, '+'); }
static int c_store_sub(rpn_ctx_t *c) { return store_op(c, '-'); }
static int c_store_mul(rpn_ctx_t *c) { return store_op(c, '*'); }
static int c_store_div(rpn_ctx_t *c) { return store_op(c, '/'); }

static int c_clearvars(rpn_ctx_t *c)
{
    c->nvars = 0;
    return RPN_OK;
}

static int c_view(rpn_ctx_t *c)
{
    const rpn_value_t *v = rpn_var_lookup(c, c->arg);
    char buf[RPN_FORMAT_MAX];
    char line[RPN_FORMAT_MAX + RPN_MAX_TOKEN + 8];

    if (!v) {
        return RPN_E_UNDEFINED;
    }
    if (rpn_format(c, v, buf, sizeof buf) != 0) {
        return RPN_E_TYPE;
    }
    if (c->env.out != NULL) {
        strcpy(line, c->arg);
        strcat(line, " = ");
        strcat(line, buf);
        strcat(line, "\n");
        c->env.out(c->env.user, line);
    }
    return RPN_OK;
}

/* ---- loop counters ------------------------------------------------------- */

/*
 * Splits a counter into ccc (sign kept), fff and ii.  `frac` is the
 * magnitude of the fractional part, to be put back after the update.
 */
static int split_counter(const rpn_value_t *v, long *ccc, long *fff, long *ii,
                         double *frac)
{
    double d, ip, fp;
    long code;

    *fff = 0;
    *ii = 1;
    *frac = 0.0;
    if (v->tag == RPN_T_INT) {
        *ccc = v->u.i;
        return RPN_OK;
    }
    if (v->tag != RPN_T_REAL) {
        return RPN_E_TYPE;
    }
    d = v->u.r;
    if (fabs(d) >= 1e9) {
        return RPN_E_DOMAIN;
    }
    fp = fabs(modf(d, &ip));
    *ccc = (long)ip;
    *frac = fp;
    code = (long)floor(fp * 100000.0 + 0.5);       /* fffii */
    *fff = code / 100;
    *ii = code % 100;
    if (*ii == 0) {
        *ii = 1;
    }
    return RPN_OK;
}

/* Writes the new counter back, keeping the type and the fraction. */
static int store_counter(rpn_ctx_t *c, const rpn_value_t *old, long ccc,
                         double frac, int negative)
{
    rpn_value_t nv;

    if (old->tag == RPN_T_INT) {
        nv = rpn_value_int(ccc);
    } else {
        nv = rpn_value_real((negative || ccc < 0) ? (double)ccc - frac
                                                  : (double)ccc + frac);
    }
    return rpn_var_set(c, c->arg, &nv);
}

static int counter(rpn_ctx_t *c, int dir)
{
    const rpn_value_t *cur = rpn_var_lookup(c, c->arg);
    rpn_value_t old;
    long ccc, fff, ii;
    double frac;
    int rc;

    if (!cur) {
        return RPN_E_UNDEFINED;
    }
    old = *cur;
    if (old.tag == RPN_T_INT && (old.u.i > 1000000000L || old.u.i < -1000000000L)) {
        return RPN_E_INT_OVERFLOW;
    }
    rc = split_counter(&old, &ccc, &fff, &ii, &frac);
    if (rc != RPN_OK) {
        return rc;
    }
    ccc += dir * ii;
    rc = store_counter(c, &old, ccc, frac, old.tag == RPN_T_REAL && old.u.r < 0);
    if (rc != RPN_OK) {
        return rc;
    }
    c->skip = (dir < 0) ? (ccc <= fff) : (ccc > fff);
    return RPN_OK;
}

static int c_dse(rpn_ctx_t *c) { return counter(c, -1); }
static int c_isg(rpn_ctx_t *c) { return counter(c, 1); }

const rpn_cmd_t rpn_var_cmds[] = {
    { "store",   "sto",  1, CMD_ARG_NAME, c_store     },
    { "recall",  "rcl",  0, CMD_ARG_NAME, c_recall    },
    { "store+",  "sto+", 1, CMD_ARG_NAME, c_store_add },
    { "store-",  "sto-", 1, CMD_ARG_NAME, c_store_sub },
    { "store*",  "sto*", 1, CMD_ARG_NAME, c_store_mul },
    { "store/",  "sto/", 1, CMD_ARG_NAME, c_store_div },
    { "clearvars", NULL, 0, 0,            c_clearvars },
    { "view",    NULL,   0, CMD_ARG_NAME, c_view      },
    { "decrement-skip-zero", "dse", 0, CMD_ARG_NAME, c_dse },
    { "increment-skip-greater", "isg", 0, CMD_ARG_NAME, c_isg },
    { NULL,      NULL,   0, 0,            NULL        }
};
