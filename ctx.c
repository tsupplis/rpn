/*
 * ctx.c - calculator context lifetime, error reporting and rpn_exec().
 */
#include <stdio.h>
#include <string.h>
#include "intern.h"

static const char *const err_text[RPN_E_COUNT] = {
    "ok",
    "out of memory",
    "stack overflow",
    "stack underflow",
    "division by zero",
    "integer overflow",
    "real overflow",
    "domain error",
    "wrong value type",
    "bad argument",
    "unknown command",
    "token too long",
    "invalid name",
    "undefined name",
    "duplicate name",
    "not valid here",
    "call depth exceeded",
    "step limit exceeded",
    "label out of scope"
};

const char *rpn_strerror(int code)
{
    if (code < 0 || code >= RPN_E_COUNT) {
        return "unknown error";
    }
    return err_text[code];
}

int rpn_fail(rpn_ctx_t *c, int code, const char *what)
{
    c->err = code;
    c->reported = 1;
    sprintf(c->errmsg, "%.40s (%.64s)", rpn_strerror(code), what);
    if (c->env.error != NULL) {
        c->env.error(c->env.user, code, c->errmsg);
    }
    return code;
}

rpn_ctx_t *rpn_new(const rpn_env_t *env)
{
    rpn_ctx_t *c;

    if (!env || !env->alloc || !env->release) {
        return NULL;
    }
    c = (rpn_ctx_t *)env->alloc(env->user, sizeof *c);
    if (c == NULL) {
        return NULL;
    }
    memset(c, 0, sizeof *c);
    c->env = *env;
    c->max = RPN_DEFAULT_STACK_MAX;
    c->angle = ANGLE_RAD;
    c->disp = DISP_ALL;
    c->digits = 12;
    c->strict = 0;
    c->lastx = rpn_value_int(0L);
    c->max_calls = RPN_DEFAULT_CALL_LIMIT;
    c->max_steps = RPN_DEFAULT_STEP_LIMIT;
    return c;
}

void rpn_free(rpn_ctx_t *c)
{
    rpn_env_t env;

    if (c == NULL) {
        return;
    }
    env = c->env;
    if (c->stack != NULL) {
        env.release(env.user, c->stack);
    }
    if (c->vars != NULL) {
        env.release(env.user, c->vars);
    }
    if (c->prog != NULL) {
        env.release(env.user, c->prog);
    }
    if (c->frames != NULL) {
        env.release(env.user, c->frames);
    }
    if (c->locs != NULL) {
        env.release(env.user, c->locs);
    }
    if (c->syms != NULL) {
        env.release(env.user, c->syms);
    }
    env.release(env.user, c);
}

int rpn_last_error(const rpn_ctx_t *c)
{
    return c->err;
}

const char *rpn_last_error_message(const rpn_ctx_t *c)
{
    return c->errmsg;
}

void *rpn_grow(rpn_ctx_t *c, void *buf, size_t *cap, size_t elem,
               size_t need, size_t used)
{
    size_t ncap = *cap ? *cap : 8;
    void *nb;

    if (need <= *cap) {
        return buf;
    }
    while (ncap < need) {
        if (ncap > (size_t)-1 / 2) {
            return NULL;
        }
        ncap *= 2;
    }
    if (ncap > (size_t)-1 / elem) {
        return NULL;
    }
    nb = c->env.alloc(c->env.user, ncap * elem);
    if (nb == NULL) {
        return NULL;
    }
    if (used > 0) {
        memcpy(nb, buf, used * elem);
    }
    if (buf != NULL) {
        c->env.release(c->env.user, buf);
    }
    *cap = ncap;
    return nb;
}

int rpn_run_cmd(rpn_ctx_t *c, const rpn_cmd_t *cmd, const char *what)
{
    rpn_value_t old_top;
    int rc;

    if (c->depth < cmd->arity) {
        return rpn_fail(c, RPN_E_STACK_UNDERFLOW, what);
    }
    old_top = c->lastx;
    if (cmd->arity > 0) {
        old_top = STK_AT(c, 0);
    }
    c->reported = 0;
    rc = cmd->fn(c);
    if (rc != RPN_OK) {
        if (!c->reported) {
            rpn_fail(c, rc, what);
        }
        return rc;
    }
    if (cmd->flags & CMD_LASTX) {
        c->lastx = old_top;
    }
    return RPN_OK;
}

int rpn_exec(rpn_ctx_t *c, const char *text)
{
    char tok[RPN_MAX_TOKEN + 1];
    const char *p = text;
    int r, rc;

    if (c == NULL) {
        return RPN_E_BAD_ARG;
    }
    c->err = RPN_OK;
    c->errmsg[0] = '\0';
    c->reported = 0;
    if (!text) {
        return rpn_fail(c, RPN_E_BAD_ARG, "null text");
    }
    for (;;) {
        r = rpn_next_token(&p, tok, sizeof tok);
        if (r == 0) {
            return RPN_OK;
        }
        if (r < 0) {
            tok[16] = '\0';
            rpn_abort(c);
            return rpn_fail(c, RPN_E_TOKEN_TOO_LONG, tok);
        }
        rc = rpn_feed(c, tok);
        if (rc != RPN_OK) {
            rpn_abort(c);
            return rc;
        }
    }
}
