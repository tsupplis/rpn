/*
 * intern.h - internal declarations shared by the librpn modules.
 *
 * Not part of the public API.  File names follow the 8.3 convention.
 *
 * Modules:
 *   ctx.c     context lifetime, error reporting, rpn_exec()
 *   lexer.c   tokenizer
 *   value.c   value constructors, number parsing and formatting
 *   stack.c   stack storage, public stack API, stack commands
 *   arith.c   arithmetic commands
 *   complex.c complex arithmetic helpers
 *   mathfn.c  log, trig, hyperbolic, rounding, constants, modes
 *   cmd.c     command registry
 *   prog.c    token engine, program memory, labels, functions, calls
 *   vars.c    variables, storage arithmetic, loop helpers
 *   cond.c    tests ending in "?", flags
 */
#ifndef INTERN_H
#define INTERN_H

#include <stddef.h>
#include "rpn.h"
#include "compat.h"

#define ERRMSG_MAX 128

/* Angle modes, display modes. */
#define ANGLE_RAD 0
#define ANGLE_DEG 1
#define ANGLE_GRAD 2
#define DISP_ALL 0
#define DISP_FIX 1
#define DISP_SCI 2
#define DISP_ENG 3
#define DISP_DIGITS_MAX 11

/* A variable: the name is [a-z][a-z0-9_]* and at most RPN_MAX_TOKEN long. */
typedef struct var_t {
    char name[RPN_MAX_TOKEN + 1];
    rpn_value_t v;
} var_t;

/*
 * A call frame: what a call must restore when it returns.  Calling a
 * function (isfunc) also opens a new set of locals and a new label scope;
 * "execute { label }" only saves the return address and stays in the scope.
 */
typedef struct frame_t {
    size_t ret;           /* where to resume                             */
    size_t lbase;         /* the caller's first local                    */
    size_t sstart;        /* the caller's label scope (function body)    */
    int in_func;          /* the caller was inside a function            */
    int isfunc;
} frame_t;

/*
 * One entry of the symbol table: a label or a function stored in program
 * memory.  Offsets point into prog, so they survive its reallocation.
 */
#define SYM_LABEL 0
#define SYM_FUNC 1
typedef struct sym_t {
    size_t name;          /* offset of the name token                    */
    size_t scope;         /* label: body offset of its function, 0: global */
    size_t after;         /* label: offset after its group; function: body */
    int kind;
} sym_t;

/* The calculator state.  There is no state outside of it. */
struct rpn_ctx_t {
    rpn_env_t env;
    rpn_value_t *stack;   /* stack[depth-1] is level 0 (the top)         */
    size_t depth;
    size_t cap;           /* allocated entries                           */
    size_t max;           /* configured maximum depth                    */
    rpn_value_t lastx;
    int angle;            /* ANGLE_*; applies to real arguments only     */
    int disp;             /* DISP_*                                      */
    int digits;           /* digits for fix, sci and eng                 */
    int strict;           /* 1: out-of-domain real input is an error     */
    int err;
    char errmsg[ERRMSG_MAX];
    int reported;         /* an error was reported since the command began */

    /* variables and flags (vars.c, cond.c) */
    var_t *vars;
    size_t nvars;
    size_t vcap;
    unsigned long flags;  /* flags 0..RPN_MAX_FLAG, one bit each         */

    /* token engine and programs (prog.c) */
    char *prog;           /* program memory: NUL-terminated tokens       */
    size_t prog_len;
    size_t prog_cap;
    sym_t *syms;          /* labels and functions in prog, in order      */
    size_t nsyms;
    size_t scap;
    int prog_mode;        /* 1 while a program block is being entered    */
    const struct rpn_cmd_t *gcmd;      /* command whose { group } is open  */
    int gphase;           /* 0: expecting "{"   1: inside the group      */
    int gcount;           /* items read so far in the group              */
    size_t gstart;        /* where the command was stored (rollback)     */
    size_t gitems;        /* offset of the first item in prog            */
    char arg[RPN_MAX_TOKEN + 1];       /* item of the group just read      */
    int skip;             /* a test failed: skip the next command        */
    int skip_grp;         /* the group being read belongs to a skipped command */
    long argn;            /* the item of a CMD_ARG_FLAG group            */
    int running;          /* a program is running                        */
    int halt;             /* set by return/stop to end the run           */
    size_t pc;            /* offset of the next token in prog            */
    frame_t *frames;      /* the call stack                              */
    size_t nframes;
    size_t fcap;
    var_t *locs;          /* locals of the running functions             */
    size_t nlocs;
    size_t locap;
    size_t lbase;         /* first local of the current function         */
    int in_func;          /* label scope: a function body, or global     */
    size_t sstart;        /* where that body starts in prog              */
    int def_func;         /* the block being entered is a function       */
    size_t def_body;      /* where its body starts in prog               */
    size_t max_calls;
    unsigned long steps;
    unsigned long max_steps;
};

/* ---- command registry ------------------------------------------- */

typedef int (*rpn_handler_t)(rpn_ctx_t *c);

/*
 * Command flags.  A command with a group flag is followed in the text by
 * a { group }; the command and its group form one atom.  Whatever is not
 * written in a group comes from the stack.
 */
#define CMD_LASTX 1      /* on success, LASTX receives the old top value */
#define CMD_ARG_NAME 2   /* { name }: a variable                         */
#define CMD_ARG_FLAG 4   /* { n }: a flag number                         */
#define CMD_ARG_LABEL 8  /* { name }: a label                            */
#define CMD_LIST 16      /* { name name ... }: handler reads them        */
#define CMD_BLOCK 32     /* { body }: program, handled by the engine     */
#define CMD_FUNC_DEF 64  /* { name body }: function, by the engine       */
#define CMD_ARGMASK (CMD_ARG_NAME | CMD_ARG_FLAG | CMD_ARG_LABEL | \
                     CMD_LIST | CMD_BLOCK | CMD_FUNC_DEF)
#define RPN_MAX_FLAG 31

/*
 * One registry row.  `name` is the canonical full word; `alias` (may be
 * NULL) is a legacy mnemonic accepted on input only.  The engine checks
 * that `arity` values are present before calling `fn`, so a handler may
 * assume them and a failing command leaves the stack unchanged as long as
 * the handler computes its result before modifying the stack.
 */
typedef struct rpn_cmd_t {
    const char *name;
    const char *alias;
    size_t arity;
    int flags;
    rpn_handler_t fn;
} rpn_cmd_t;

/* Tables end with a row whose name is NULL. */
extern const rpn_cmd_t rpn_stack_cmds[];
extern const rpn_cmd_t rpn_arith_cmds[];
extern const rpn_cmd_t rpn_math_cmds[];
extern const rpn_cmd_t rpn_prog_cmds[];
extern const rpn_cmd_t rpn_var_cmds[];
extern const rpn_cmd_t rpn_cond_cmds[];

/* Finds a command by name or alias; NULL if unknown. */
const rpn_cmd_t *rpn_cmd_find(const char *name);

/*
 * Runs one registry command: checks the arity, calls the handler, keeps
 * LASTX and reports the error (once) as "<description> (what)".
 */
int rpn_run_cmd(rpn_ctx_t *c, const rpn_cmd_t *cmd, const char *what);

/* ---- token engine, programs (prog.c) ------------------------------ */

/*
 * Processes one lower-case token: executes it, or stores it while a
 * program is being entered.  A non-zero result has already been reported.
 */
int rpn_feed(rpn_ctx_t *c, const char *tok);

/*
 * Called after an error: forgets a half-read group (and removes it from
 * program memory), and clears the skip state.  A block being entered stays
 * open.
 */
void rpn_abort(rpn_ctx_t *c);

/* A name usable for a variable or local: not a command or function. */
int rpn_valid_name(const rpn_ctx_t *c, const char *s);

/*
 * Finds a function.  *body is the offset of the first token after its name
 * (body may be NULL).
 */
int rpn_func_find(const rpn_ctx_t *c, const char *name, size_t *body);

/* ---- variables (vars.c) ------------------------------------------- */

/* Lookup and set try the locals of the running function first. */
const rpn_value_t *rpn_var_lookup(const rpn_ctx_t *c, const char *name);
int rpn_var_set(rpn_ctx_t *c, const char *name, const rpn_value_t *v);

/* Defines (or overwrites) a local in the current call frame. */
int rpn_local_def(rpn_ctx_t *c, const char *name, const rpn_value_t *v);

/* y op x for op one of + - * / ; no stack change (arith.c). */
int rpn_binary_op(int op, const rpn_value_t *y, const rpn_value_t *x,
                  rpn_value_t *out);

/* ---- memory ----------------------------------------------------- */

/*
 * Grows an array so it holds at least `need` elements of size `elem`
 * (`*cap` is its capacity, `used` the elements to keep).  Returns the new
 * array, or NULL if memory is exhausted (the old array is then intact).
 */
void *rpn_grow(rpn_ctx_t *c, void *buf, size_t *cap, size_t elem,
               size_t need, size_t used);

/* ---- stack helpers ---------------------------------------------- */

/* Pushes with overflow check; may allocate. */
int rpn_stk_push(rpn_ctx_t *c, const rpn_value_t *v);

/* Level 0 is the top.  The caller guarantees level < depth. */
#define STK_AT(c, level) ((c)->stack[(c)->depth - 1 - (level)])

/* ---- errors ----------------------------------------------------- */

/*
 * Records and reports an error: stores "<description> (<what>)" as the
 * last error message and calls env.error.  Returns `code`.
 */
int rpn_fail(rpn_ctx_t *c, int code, const char *what);

/* ---- numbers ---------------------------------------------------- */

/* Like rpn_parse_number() but reports the error code on -1. */
int rpn_num_parse(const char *token, rpn_value_t *out, int *err);

/* ---- complex helpers (complex.c) -------------------------------- */

typedef struct cx_t {
    double re;
    double im;
} cx_t;

void cx_set(cx_t *r, double re, double im);
double cx_hypot(double a, double b);
double cx_abs(const cx_t *z);
double cx_arg(const cx_t *z);

/* The result may alias an operand.  Each returns RPN_OK or an error code. */
int cx_add(cx_t *r, const cx_t *a, const cx_t *b);
int cx_sub(cx_t *r, const cx_t *a, const cx_t *b);
int cx_mul(cx_t *r, const cx_t *a, const cx_t *b);
int cx_div(cx_t *r, const cx_t *a, const cx_t *b);
int cx_exp(cx_t *r, const cx_t *z);
int cx_log(cx_t *r, const cx_t *z);
int cx_sqrt(cx_t *r, const cx_t *z);
int cx_pow(cx_t *r, const cx_t *a, const cx_t *b);
int cx_sin(cx_t *r, const cx_t *z);
int cx_cos(cx_t *r, const cx_t *z);
int cx_tan(cx_t *r, const cx_t *z);
int cx_sinh(cx_t *r, const cx_t *z);
int cx_cosh(cx_t *r, const cx_t *z);
int cx_tanh(cx_t *r, const cx_t *z);
int cx_asin(cx_t *r, const cx_t *z);
int cx_acos(cx_t *r, const cx_t *z);
int cx_atan(cx_t *r, const cx_t *z);
int cx_asinh(cx_t *r, const cx_t *z);
int cx_acosh(cx_t *r, const cx_t *z);
int cx_atanh(cx_t *r, const cx_t *z);

/* Value helpers shared by the command modules. */
int rpn_is_num(const rpn_value_t *v);             /* int, real or complex */
double rpn_to_d(const rpn_value_t *v);            /* int or real only     */
void rpn_to_cx(const rpn_value_t *v, cx_t *z);    /* any number           */
int rpn_ret_real(rpn_value_t *out, double r);     /* rejects NaN, inf     */
int rpn_ret_cx(rpn_value_t *out, const cx_t *z);  /* rejects NaN, inf     */

#endif /* INTERN_H */
