/*
 * prog.c - the token engine, program memory, labels, functions, calls and
 * run limits.
 *
 * Syntax.  Everything that is written in the text, a name or a number, is
 * written inside braces after its command; everything that is computed
 * comes from the stack.  A command and its group form one atom:
 *
 *     store { x }   recall { x }   goto { loop }   label { loop }
 *     setflag { 3 }   local { a b c }
 *     function { name body ... }   program { body ... }
 *
 * Every token given to rpn_exec() ends up in rpn_feed(), which decides
 * what it is:
 *
 *   - while a block ("program { ... }" or "function { ... }") is being
 *     entered, the token is stored in program memory, not executed;
 *   - otherwise it is executed: a number is pushed, a command runs, a
 *     variable name pushes the variable's value and a function name calls
 *     the function.
 *
 * A command that takes a group is remembered in c->gcmd while its group is
 * read, one token at a time (the group may continue on a later line or in
 * a later rpn_exec() call).  The command runs when the closing brace
 * arrives.
 *
 * Skipping: a test that fails sets c->skip.  The next atom (a command with
 * its group, if it has one) is then consumed without running.  The flag
 * survives line boundaries, is cleared by an error and when a run ends, so
 * it never leaks out of a program.
 *
 * Program memory is a sequence of NUL-terminated tokens, always in their
 * canonical spelling (aliases are expanded as they are stored).  A program
 * block stores its body, followed by a "return"; a function is stored as
 * "function { name body }" and found by scanning the memory, like labels.
 * A run is a flat loop over the tokens; calls use a separate frame stack,
 * so C recursion stays shallow (important on small machines).
 *
 * Scopes: a label outside any function is global and can be reached from
 * global code only.  A label inside a function body can be reached from
 * that function only.  A function call opens a new set of locals ("local
 * { a b }" pops the stack into named locals) and a new label scope;
 * "execute { label }" is a plain subroutine call that shares both.
 */
#include <stdio.h>
#include <string.h>
#include "intern.h"

/* ---- names ------------------------------------------------------------- */

/* [a-z][a-z0-9_]* ; the lexer already lower-cased the token. */
static int is_name_syntax(const char *s)
{
    if (!(s[0] >= 'a' && s[0] <= 'z')) {
        return 0;
    }
    for (s++; *s != '\0'; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') ||
              *s == '_')) {
            return 0;
        }
    }
    return 1;
}

/* A label has its own namespace: it only may not be a command word. */
static int valid_label(const char *s)
{
    return is_name_syntax(s) && !rpn_cmd_find(s);
}

int rpn_valid_name(const rpn_ctx_t *c, const char *s)
{
    return is_name_syntax(s) && !rpn_cmd_find(s) &&
           !rpn_func_find(c, s, NULL);
}

/* ---- program memory ------------------------------------------------- */

/* Appends one or two tokens; nothing is stored if memory runs out. */
static int store_tokens(rpn_ctx_t *c, const char *a, const char *b)
{
    size_t na = strlen(a) + 1;
    size_t nb = b ? strlen(b) + 1 : 0;
    char *np;

    np = (char *)rpn_grow(c, c->prog, &c->prog_cap, 1,
                          c->prog_len + na + nb, c->prog_len);
    if (np == NULL) {
        return RPN_E_NOMEM;
    }
    c->prog = np;
    memcpy(c->prog + c->prog_len, a, na);
    c->prog_len += na;
    if (b) {
        memcpy(c->prog + c->prog_len, b, nb);
        c->prog_len += nb;
    }
    return RPN_OK;
}

/* Records a label or a function in the symbol table. */
static int add_sym(rpn_ctx_t *c, int kind, size_t name, size_t scope,
                   size_t after)
{
    sym_t *ns;
    sym_t *s;

    ns = (sym_t *)rpn_grow(c, c->syms, &c->scap, sizeof(sym_t),
                           c->nsyms + 1, c->nsyms);
    if (ns == NULL) {
        return RPN_E_NOMEM;
    }
    c->syms = ns;
    s = &c->syms[c->nsyms++];
    s->kind = kind;
    s->name = name;
    s->scope = scope;
    s->after = after;
    return RPN_OK;
}

/*
 * Finds label `name` in the scope described by in_func and fstart: either
 * the global scope (in_func = 0) or the function whose body starts at
 * offset fstart.  Returns 1 and sets *after to the offset of the token
 * that follows the label; 2 if the label exists only in another scope;
 * else 0.
 */
static int find_label(const rpn_ctx_t *c, const char *name, int in_func,
                      size_t fstart, size_t *after)
{
    size_t scope = in_func ? fstart : 0;
    size_t i;
    int other = 0;

    for (i = 0; i < c->nsyms; i++) {
        const sym_t *s = &c->syms[i];

        if (s->kind != SYM_LABEL || strcmp(c->prog + s->name, name) != 0) {
            continue;
        }
        if (s->scope == scope) {
            *after = s->after;
            return 1;
        }
        other = 1;
    }
    return other ? 2 : 0;
}

int rpn_func_find(const rpn_ctx_t *c, const char *name, size_t *body)
{
    size_t i;

    for (i = 0; i < c->nsyms; i++) {
        const sym_t *s = &c->syms[i];

        if (s->kind == SYM_FUNC && strcmp(c->prog + s->name, name) == 0) {
            if (body) {
                *body = s->after;
            }
            return 1;
        }
    }
    return 0;
}

int rpn_in_program(const rpn_ctx_t *c)
{
    return c->prog_mode || (c->gcmd ? 1 : 0);
}

void rpn_abort(rpn_ctx_t *c)
{
    if (c->gcmd && c->prog_mode && !c->running) {
        c->prog_len = c->gstart;            /* forget the half-entered atom */
    }
    c->gcmd = NULL;
    c->gphase = 0;
    c->gcount = 0;
    c->skip = 0;
    c->skip_grp = 0;
}

/* ---- groups ----------------------------------------------------------------- */

/* Validates the item of a group; stores a flag number in c->argn. */
static int check_item(const rpn_ctx_t *c, const rpn_cmd_t *cmd,
                      const char *tok, long *n)
{
    rpn_value_t v;
    int err = RPN_OK;
    size_t o;

    if (cmd->flags & CMD_ARG_LABEL) {
        return valid_label(tok) ? RPN_OK : RPN_E_BAD_NAME;
    }
    if (cmd->flags & (CMD_ARG_NAME | CMD_LIST)) {
        if (!rpn_valid_name(c, tok)) {
            return RPN_E_BAD_NAME;
        }
        if ((cmd->flags & CMD_LIST) && c->prog_mode && !c->running) {
            for (o = c->gitems; o < c->prog_len;
                 o += strlen(c->prog + o) + 1) {
                if (strcmp(c->prog + o, tok) == 0) {
                    return RPN_E_DUPLICATE;
                }
            }
        }
        return RPN_OK;
    }
    if (cmd->flags & CMD_ARG_FLAG) {
        if (rpn_num_parse(tok, &v, &err) != 1 || v.tag != RPN_T_INT ||
            v.u.i < 0 || v.u.i > RPN_MAX_FLAG) {
            return RPN_E_BAD_ARG;
        }
        *n = v.u.i;
    }
    return RPN_OK;
}

/* The name of a new function: not a command, a function or a variable. */
static int check_new_function(const rpn_ctx_t *c, const char *tok)
{
    if (!is_name_syntax(tok) || rpn_cmd_find(tok)) {
        return RPN_E_BAD_NAME;
    }
    if (rpn_func_find(c, tok, NULL)) {
        return RPN_E_DUPLICATE;
    }
    if (rpn_var_lookup(c, tok)) {
        return RPN_E_BAD_NAME;
    }
    return RPN_OK;
}

static int group_fail(rpn_ctx_t *c, int code, const rpn_cmd_t *cmd,
                      const char *tok)
{
    char what[RPN_MAX_TOKEN + 32];

    sprintf(what, "%s %s", cmd->name, tok);
    return rpn_fail(c, code, what);
}

/* One token of a "command { ... }" group, executed or being entered. */
static int group_token(rpn_ctx_t *c, const char *tok)
{
    const rpn_cmd_t *cmd = c->gcmd;
    int entering = c->prog_mode && !c->running;
    int single = (cmd->flags & (CMD_ARG_NAME | CMD_ARG_FLAG | CMD_ARG_LABEL))
                 != 0;
    char what[RPN_MAX_TOKEN + 32];
    size_t off, keep;
    int rc;

    if (c->gphase == 0) {
        if (strcmp(tok, "{") != 0) {
            return group_fail(c, RPN_E_BAD_ARG, cmd, tok);
        }
        if (cmd->flags & CMD_BLOCK) {              /* program { body }     */
            c->gcmd = NULL;
            c->prog_mode = 1;
            c->def_func = 0;
            return RPN_OK;
        }
        if (entering) {
            rc = store_tokens(c, tok, NULL);
            if (rc != RPN_OK) {
                return rpn_fail(c, rc, tok);
            }
        }
        c->gphase = 1;
        c->gcount = 0;
        c->gitems = entering ? c->prog_len : c->pc;
        return RPN_OK;
    }
    if (strcmp(tok, "{") == 0) {
        return group_fail(c, RPN_E_BAD_ARG, cmd, tok);
    }
    if (strcmp(tok, "}") == 0) {
        if (c->gcount == 0 || (cmd->flags & CMD_FUNC_DEF)) {
            return group_fail(c, RPN_E_BAD_ARG, cmd, tok);
        }
        if (entering) {
            rc = store_tokens(c, tok, NULL);
            if (rc == RPN_OK && strcmp(cmd->name, "label") == 0) {
                rc = add_sym(c, SYM_LABEL, c->gitems,
                             c->def_func ? c->def_body : 0, c->prog_len);
            }
            if (rc != RPN_OK) {
                c->prog_len = c->gstart;
            }
            c->gcmd = NULL;
            c->gphase = 0;
            return rc == RPN_OK ? RPN_OK : rpn_fail(c, rc, tok);
        }
        c->gcmd = NULL;
        c->gphase = 0;
        if (c->skip_grp) {
            c->skip_grp = 0;
            return RPN_OK;
        }
        sprintf(what, "%s %s", cmd->name, c->arg);
        return rpn_run_cmd(c, cmd, what);
    }
    /* an item */
    if (single && c->gcount >= 1) {
        return group_fail(c, RPN_E_BAD_ARG, cmd, tok);
    }
    if (cmd->flags & CMD_FUNC_DEF) {               /* function { name ...  */
        rc = check_new_function(c, tok);
        if (rc != RPN_OK) {
            return group_fail(c, rc, cmd, tok);
        }
        keep = c->prog_len;
        rc = store_tokens(c, cmd->name, "{");
        if (rc == RPN_OK) {
            off = c->prog_len;
            rc = store_tokens(c, tok, NULL);
        }
        if (rc == RPN_OK) {
            rc = add_sym(c, SYM_FUNC, off, 0, c->prog_len);
        }
        if (rc != RPN_OK) {
            c->prog_len = keep;                     /* all or nothing */
            return group_fail(c, rc, cmd, tok);
        }
        c->gcmd = NULL;
        c->prog_mode = 1;
        c->def_func = 1;
        c->def_body = c->prog_len;
        return RPN_OK;
    }
    rc = check_item(c, cmd, tok, &c->argn);
    if (rc == RPN_OK && entering && (cmd->flags & CMD_ARG_LABEL) &&
        strcmp(cmd->name, "label") == 0 &&
        find_label(c, tok, c->def_func, c->def_body, &off) == 1) {
        rc = RPN_E_DUPLICATE;
    }
    if (rc == RPN_OK && entering) {
        rc = store_tokens(c, tok, NULL);
    }
    if (rc != RPN_OK) {
        return group_fail(c, rc, cmd, tok);
    }
    strcpy(c->arg, tok);
    c->gcount++;
    return RPN_OK;
}

/* ---- entering a block ------------------------------------------------------ */

static int enter_token(rpn_ctx_t *c, const char *tok)
{
    const rpn_cmd_t *cmd;
    rpn_value_t v;
    size_t start = c->prog_len;
    int err = RPN_OK;
    int rc;

    if (strcmp(tok, "}") == 0) {                   /* end of the block     */
        rc = store_tokens(c, c->def_func ? "}" : "return", NULL);
        if (rc != RPN_OK) {
            return rpn_fail(c, rc, tok);
        }
        c->prog_mode = 0;
        c->def_func = 0;
        return RPN_OK;
    }
    if (strcmp(tok, "{") == 0) {
        return rpn_fail(c, RPN_E_BAD_ARG, tok);
    }
    rc = rpn_num_parse(tok, &v, &err);
    if (rc < 0) {
        return rpn_fail(c, err, tok);
    }
    if (rc == 1) {
        rc = store_tokens(c, tok, NULL);
        return rc == RPN_OK ? RPN_OK : rpn_fail(c, rc, tok);
    }
    cmd = rpn_cmd_find(tok);
    if (cmd) {
        if (cmd->flags & (CMD_BLOCK | CMD_FUNC_DEF)) {
            return rpn_fail(c, RPN_E_PROGRAM, "program or function inside a "
                                              "block");
        }
        if (strcmp(cmd->name, "local") == 0 && !c->def_func) {
            return rpn_fail(c, RPN_E_PROGRAM, "local outside a function");
        }
        rc = store_tokens(c, cmd->name, NULL);
        if (rc != RPN_OK) {
            return rpn_fail(c, rc, tok);
        }
        if (cmd->flags & CMD_ARGMASK) {
            c->gcmd = cmd;
            c->gphase = 0;
            c->gcount = 0;
            c->gstart = start;
        }
        return RPN_OK;
    }
    if (is_name_syntax(tok)) {        /* a variable, which may not exist yet */
        rc = store_tokens(c, tok, NULL);
        return rc == RPN_OK ? RPN_OK : rpn_fail(c, rc, tok);
    }
    return rpn_fail(c, RPN_E_UNKNOWN_CMD, tok);
}

/* ---- running a program --------------------------------------------------- */

static int run_program(rpn_ctx_t *c, size_t off, int in_func, size_t sstart);

/* Consumes a command that a failed test asked to skip. */
static int skipped(rpn_ctx_t *c)
{
    if (c->skip) {
        c->skip = 0;
        return 1;
    }
    return 0;
}

/* Saves the caller's state on the frame stack. */
static int push_frame(rpn_ctx_t *c, size_t ret, int isfunc)
{
    frame_t *nf;
    frame_t *f;

    if (c->nframes >= c->max_calls) {
        return RPN_E_CALL_DEPTH;
    }
    nf = (frame_t *)rpn_grow(c, c->frames, &c->fcap, sizeof(frame_t),
                             c->nframes + 1, c->nframes);
    if (nf == NULL) {
        return RPN_E_NOMEM;
    }
    c->frames = nf;
    f = &c->frames[c->nframes++];
    f->ret = ret;
    f->lbase = c->lbase;
    f->sstart = c->sstart;
    f->in_func = c->in_func;
    f->isfunc = isfunc;
    return RPN_OK;
}

/* Calls the function whose body starts at `body`.  Starts a run at the top. */
static int call_func(rpn_ctx_t *c, size_t body)
{
    int rc;

    if (!c->running) {
        return run_program(c, body, 1, body);
    }
    rc = push_frame(c, c->pc, 1);
    if (rc != RPN_OK) {
        return rc;
    }
    c->lbase = c->nlocs;
    c->in_func = 1;
    c->sstart = body;
    c->pc = body;
    return RPN_OK;
}

/* Common to return and the closing brace of a function. */
static int do_return(rpn_ctx_t *c)
{
    const frame_t *f;

    if (c->nframes == 0) {
        c->halt = 1;
        return RPN_OK;
    }
    f = &c->frames[--c->nframes];
    if (f->isfunc) {
        c->nlocs = c->lbase;            /* the callee's locals go away */
    }
    c->pc = f->ret;
    c->lbase = f->lbase;
    c->sstart = f->sstart;
    c->in_func = f->in_func;
    return RPN_OK;
}

static int dispatch(rpn_ctx_t *c, const char *tok)
{
    const rpn_cmd_t *cmd;
    const rpn_value_t *var;
    rpn_value_t v;
    size_t body;
    int err = RPN_OK;
    int rc;

    if (strcmp(tok, "}") == 0) {
        if (!c->running) {
            return rpn_fail(c, RPN_E_PROGRAM, tok);
        }
        c->skip = 0;                    /* the end of a function returns */
        return do_return(c);
    }
    if (strcmp(tok, "{") == 0) {
        return rpn_fail(c, RPN_E_BAD_ARG, tok);
    }
    rc = rpn_num_parse(tok, &v, &err);
    if (rc < 0) {
        return rpn_fail(c, err, tok);
    }
    if (rc == 1) {
        if (skipped(c)) {
            return RPN_OK;
        }
        rc = rpn_stk_push(c, &v);
        return rc == RPN_OK ? RPN_OK : rpn_fail(c, rc, tok);
    }
    cmd = rpn_cmd_find(tok);
    if (cmd) {
        if (cmd->flags & CMD_ARGMASK) {
            if (c->running && (cmd->flags & (CMD_BLOCK | CMD_FUNC_DEF))) {
                return rpn_fail(c, RPN_E_PROGRAM, tok);
            }
            c->gcmd = cmd;
            c->gphase = 0;
            c->gcount = 0;
            c->skip_grp = c->skip;
            c->skip = 0;
            return RPN_OK;
        }
        if (skipped(c)) {
            return RPN_OK;
        }
        return rpn_run_cmd(c, cmd, tok);
    }
    if (is_name_syntax(tok)) {
        var = rpn_var_lookup(c, tok);
        if (var) {
            if (skipped(c)) {
                return RPN_OK;
            }
            rc = rpn_stk_push(c, var);
            return rc == RPN_OK ? RPN_OK : rpn_fail(c, rc, tok);
        }
        if (rpn_func_find(c, tok, &body)) {
            if (skipped(c)) {
                return RPN_OK;
            }
            c->reported = 0;
            rc = call_func(c, body);
            if (rc != RPN_OK && !c->reported) {
                rpn_fail(c, rc, tok);
            }
            return rc;
        }
    }
    return rpn_fail(c, RPN_E_UNKNOWN_CMD, tok);
}

int rpn_feed(rpn_ctx_t *c, const char *tok)
{
    if (c->gcmd) {
        return group_token(c, tok);
    }
    if (c->prog_mode && !c->running) {
        return enter_token(c, tok);
    }
    return dispatch(c, tok);
}

/*
 * Runs from offset `off` in the given label scope until stop, return, the
 * end of memory or an error.  Only called when nothing is running yet.
 * Whatever way the run ends, the frames and the locals are released.
 */
static int run_program(rpn_ctx_t *c, size_t off, int in_func, size_t sstart)
{
    const char *tok;
    int rc = RPN_OK;

    c->running = 1;
    c->halt = 0;
    c->nframes = 0;
    c->nlocs = 0;
    c->lbase = 0;
    c->in_func = in_func;
    c->sstart = sstart;
    c->steps = 0;
    c->pc = off;
    while (!c->halt && c->pc < c->prog_len) {
        if (++c->steps > c->max_steps) {
            rc = rpn_fail(c, RPN_E_STEP_LIMIT, "program");
            break;
        }
        tok = c->prog + c->pc;
        c->pc += strlen(tok) + 1;
        rc = c->gcmd ? group_token(c, tok) : dispatch(c, tok);
        if (rc != RPN_OK) {
            break;
        }
    }
    c->running = 0;
    c->halt = 0;
    c->nframes = 0;
    c->nlocs = 0;
    c->lbase = 0;
    c->in_func = 0;
    c->sstart = 0;
    c->skip = 0;
    c->skip_grp = 0;
    c->gcmd = NULL;
    c->gphase = 0;
    return rc;
}

/* ---- commands ---------------------------------------------------------------- */

/* program and function are handled by the engine; this is never reached. */
static int c_nofn(rpn_ctx_t *c)
{
    (void)c;
    return RPN_E_PROGRAM;
}

static int c_return(rpn_ctx_t *c)
{
    return c->running ? do_return(c) : RPN_E_PROGRAM;
}

static int c_stop(rpn_ctx_t *c)
{
    if (!c->running) {
        return RPN_E_PROGRAM;
    }
    c->halt = 1;
    return RPN_OK;
}

static int c_label(rpn_ctx_t *c)
{
    return c->running ? RPN_OK : RPN_E_PROGRAM;
}

/*
 * "local { a b c }" pops the stack into locals: the last name receives the
 * top, so "1 2 3 local { a b c }" gives a = 1, b = 2, c = 3.  A local that
 * already exists in this call is overwritten.  The stack is left intact
 * unless every name can be bound.  The names are in program memory from
 * c->gitems up to the closing brace.
 */
static int c_local(rpn_ctx_t *c)
{
    size_t o, n = 0, i;
    const char *nm;
    int rc;

    if (!c->running || !c->in_func) {
        return RPN_E_PROGRAM;
    }
    for (o = c->gitems; o < c->prog_len && strcmp(c->prog + o, "}") != 0;
         o += strlen(c->prog + o) + 1) {
        n++;
    }
    if (c->depth < n) {
        return RPN_E_STACK_UNDERFLOW;
    }
    o = c->gitems;
    for (i = 0; i < n; i++) {
        nm = c->prog + o;
        rc = rpn_local_def(c, nm, &STK_AT(c, n - 1 - i));
        if (rc != RPN_OK) {
            return rc;
        }
        o += strlen(nm) + 1;
    }
    c->depth -= n;
    return RPN_OK;
}

/* Finds the label named by c->arg in the current scope. */
static int resolve_label(const rpn_ctx_t *c, size_t *off)
{
    int r = find_label(c, c->arg, c->in_func, c->sstart, off);

    if (r == 0) {
        return RPN_E_UNDEFINED;
    }
    return r == 2 ? RPN_E_SCOPE : RPN_OK;
}

/* At the top level goto and execute both start a run at the label. */
static int c_goto(rpn_ctx_t *c)
{
    size_t off;
    int rc = resolve_label(c, &off);

    if (rc != RPN_OK) {
        return rc;
    }
    if (!c->running) {
        return run_program(c, off, 0, 0);
    }
    c->pc = off;
    return RPN_OK;
}

static int c_execute(rpn_ctx_t *c)
{
    size_t off;
    int rc = resolve_label(c, &off);

    if (rc != RPN_OK) {
        return rc;
    }
    if (!c->running) {
        return run_program(c, off, 0, 0);
    }
    rc = push_frame(c, c->pc, 0);
    if (rc != RPN_OK) {
        return rc;
    }
    c->pc = off;
    return RPN_OK;
}

static int c_clearprogram(rpn_ctx_t *c)
{
    if (c->running) {
        return RPN_E_PROGRAM;
    }
    c->prog_len = 0;
    c->nsyms = 0;
    return RPN_OK;
}

/* Pops a positive int and returns it in *n. */
static int pop_limit(rpn_ctx_t *c, unsigned long *n)
{
    const rpn_value_t *x = &STK_AT(c, 0);

    if (x->tag != RPN_T_INT) {
        return RPN_E_TYPE;
    }
    if (x->u.i < 1) {
        return RPN_E_DOMAIN;
    }
    *n = (unsigned long)x->u.i;
    c->depth--;
    return RPN_OK;
}

static int c_steplimit(rpn_ctx_t *c)
{
    unsigned long n;
    int rc = pop_limit(c, &n);

    if (rc == RPN_OK) {
        c->max_steps = n;
    }
    return rc;
}

static int c_calllimit(rpn_ctx_t *c)
{
    unsigned long n;
    int rc = pop_limit(c, &n);

    if (rc == RPN_OK) {
        c->max_calls = (size_t)n;
    }
    return rc;
}

/* ---- list ------------------------------------------------------------------ */

/*
 * "list" prints the stored programs and functions through env.out, in
 * canonical spelling, as source that can be saved and run again.  Global
 * code (programs are stored without their header) is written as one
 * program { } block: program memory is flat, so this is equivalent, and
 * listing a reloaded listing gives the same text.  The implicit return
 * that ends a program is left out; loading adds it back.
 */
#define LIST_WIDTH 72

typedef struct list_t {
    rpn_ctx_t *c;
    char line[LIST_WIDTH + RPN_MAX_TOKEN + 2];
    size_t len;
    int indent;
} list_t;

static void lst_flush(list_t *l)
{
    char buf[LIST_WIDTH + RPN_MAX_TOKEN + 8];

    if (l->len == 0) {
        return;
    }
    sprintf(buf, "%*s%s\n", l->indent, "", l->line);
    l->c->env.out(l->c->env.user, buf);
    l->len = 0;
}

static void lst_put(list_t *l, const char *tok)
{
    size_t n = strlen(tok);

    if (l->len > 0 && l->len + 1 + n > LIST_WIDTH) {
        lst_flush(l);
    }
    if (l->len > 0) {
        l->line[l->len++] = ' ';
    }
    memcpy(l->line + l->len, tok, n + 1);
    l->len += n;
}

static void lst_line(list_t *l, const char *text, int indent)
{
    lst_flush(l);
    l->indent = indent;
    lst_put(l, text);
    lst_flush(l);
}

/* A label, jump, call or local list ends its line. */
static int ends_line(const char *cmd)
{
    return strcmp(cmd, "label") == 0 || strcmp(cmd, "goto") == 0 ||
           strcmp(cmd, "execute") == 0 || strcmp(cmd, "local") == 0;
}

/*
 * Writes the tokens from *o up to limit.  With stop_at_close the walk ends
 * after the "}" that closes the function body.
 */
static void lst_tokens(list_t *l, size_t *o, size_t limit, int stop_at_close)
{
    const rpn_ctx_t *c = l->c;
    const char *tok;
    const char *prev = "";
    const char *cmd = "";
    int depth = 0;

    l->indent = 2;
    while (*o < limit) {
        tok = c->prog + *o;
        *o += strlen(tok) + 1;
        if (stop_at_close && depth == 0 && strcmp(tok, "}") == 0) {
            return;
        }
        if (strcmp(tok, "{") == 0) {
            if (depth == 0) {
                cmd = prev;
            }
            depth++;
        }
        if (depth == 0 && strcmp(tok, "label") == 0) {
            lst_flush(l);                       /* a label starts a line */
        }
        lst_put(l, tok);
        if (strcmp(tok, "}") == 0) {
            if (--depth == 0 && ends_line(cmd)) {
                lst_flush(l);
            }
        } else if (depth == 0 && (strcmp(tok, "return") == 0 ||
                                  strcmp(tok, "stop") == 0)) {
            lst_flush(l);
        }
        prev = tok;
    }
}

static void lst_function(list_t *l, size_t *o)
{
    const rpn_ctx_t *c = l->c;
    char head[RPN_MAX_TOKEN + 16];
    const char *name;

    *o += strlen(c->prog + *o) + 1;                     /* function */
    *o += strlen(c->prog + *o) + 1;                     /* {        */
    name = c->prog + *o;
    *o += strlen(name) + 1;
    sprintf(head, "function { %s", name);
    lst_line(l, head, 0);
    lst_tokens(l, o, c->prog_len, 1);
    lst_flush(l);
    lst_line(l, "}", 0);
}

static void lst_global(list_t *l, size_t *o)
{
    const rpn_ctx_t *c = l->c;
    size_t start = *o;
    size_t last = start;
    size_t end;

    while (*o < c->prog_len && strcmp(c->prog + *o, "function") != 0) {
        last = *o;
        *o += strlen(c->prog + *o) + 1;
    }
    end = (strcmp(c->prog + last, "return") == 0) ? last : *o;
    if (end == start) {
        return;
    }
    lst_line(l, "program {", 0);
    lst_tokens(l, &start, end, 0);
    lst_flush(l);
    lst_line(l, "}", 0);
}

static int c_list(rpn_ctx_t *c)
{
    list_t l;
    size_t o = 0;

    if (c->env.out == NULL) {
        return RPN_OK;
    }
    l.c = c;
    l.len = 0;
    l.indent = 0;
    while (o < c->prog_len) {
        if (strcmp(c->prog + o, "function") == 0) {
            lst_function(&l, &o);
        } else {
            lst_global(&l, &o);
        }
    }
    return RPN_OK;
}

const rpn_cmd_t rpn_prog_cmds[] = {
    { "program",      NULL,  0, CMD_BLOCK,     c_nofn         },
    { "function",     NULL,  0, CMD_FUNC_DEF,  c_nofn         },
    { "label",        "lbl", 0, CMD_ARG_LABEL, c_label        },
    { "goto",         "gto", 0, CMD_ARG_LABEL, c_goto         },
    { "execute",      "xeq", 0, CMD_ARG_LABEL, c_execute      },
    { "return",       "rtn", 0, 0,             c_return       },
    { "stop",         NULL,  0, 0,             c_stop         },
    { "local",        NULL,  0, CMD_LIST,      c_local        },
    { "list",         NULL,  0, 0,             c_list         },
    { "clearprogram", "clp", 0, 0,             c_clearprogram },
    { "steplimit",    NULL,  1, 0,             c_steplimit    },
    { "calllimit",    NULL,  1, 0,             c_calllimit    },
    { NULL,           NULL,  0, 0,             NULL           }
};
