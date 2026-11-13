/*
 * rpn.h - public API of librpn, a portable RPN calculator engine.
 *
 * The library follows the HP-42S functional model with a Forth/dc style
 * growing stack.  It is written in ANSI C89 and has no global state: every
 * resource it needs (memory, output, error reporting) is supplied by the
 * host through an rpn_env_t structure (inversion of control).
 *
 * Typical use:
 *
 *     rpn_env_t env;                       (filled in by the host)
 *     rpn_ctx_t *c = rpn_new(&env);
 *     if (rpn_exec(c, "3 4 + 2 *") == RPN_OK) {
 *         rpn_value_t v;
 *         char buf[RPN_FORMAT_MAX];
 *         rpn_peek(c, 0, &v);
 *         rpn_format(c, &v, buf, sizeof buf);      -> "14"
 *     }
 *     rpn_free(c);
 *
 * The library knows nothing about line editing, continuation lines, echo or
 * the "p" print pseudo-operator: those belong to the CLI front end.
 */
#ifndef RPN_H
#define RPN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Limits                                                             */
/* ------------------------------------------------------------------ */

#define RPN_VERSION "0.1.0"

/* Longest accepted token (characters). Buffers need RPN_MAX_TOKEN + 1. */
#define RPN_MAX_TOKEN 64

/* Stack maximum used until rpn_set_stack_max() is called. */
#define RPN_DEFAULT_STACK_MAX 100

/* Run limits used until the steplimit and calllimit commands change them. */
#define RPN_DEFAULT_STEP_LIMIT 1000000UL
#define RPN_DEFAULT_CALL_LIMIT 256

/* Minimum buffer size accepted by rpn_format(). */
#define RPN_FORMAT_MAX 96

/* ------------------------------------------------------------------ */
/* Error codes                                                        */
/* ------------------------------------------------------------------ */

enum {
    RPN_OK = 0,
    RPN_E_NOMEM,           /* the host allocator returned NULL            */
    RPN_E_STACK_OVERFLOW,  /* push beyond the configured stack maximum    */
    RPN_E_STACK_UNDERFLOW, /* command needs more values than available    */
    RPN_E_DIV_ZERO,        /* division (or mod, inverse, 0^-n) by zero    */
    RPN_E_INT_OVERFLOW,    /* integer result does not fit in a long       */
    RPN_E_REAL_OVERFLOW,   /* real result or literal is not finite        */
    RPN_E_DOMAIN,          /* argument outside the function's domain      */
    RPN_E_TYPE,            /* value has the wrong type for the command    */
    RPN_E_BAD_ARG,         /* argument value or pointer is invalid        */
    RPN_E_UNKNOWN_CMD,     /* token is neither a number nor a command     */
    RPN_E_TOKEN_TOO_LONG,  /* token longer than RPN_MAX_TOKEN             */
    RPN_E_BAD_NAME,        /* not a valid variable or label name          */
    RPN_E_UNDEFINED,       /* variable or label does not exist            */
    RPN_E_DUPLICATE,       /* label, local or function defined twice      */
    RPN_E_PROGRAM,         /* command not valid here (program mode)       */
    RPN_E_CALL_DEPTH,      /* subroutine calls nested too deeply          */
    RPN_E_STEP_LIMIT,      /* a run executed too many instructions        */
    RPN_E_SCOPE,           /* label exists, but in another scope          */
    RPN_E_COUNT            /* number of codes; not an error               */
};

/* Returns a short static description of an error code. */
const char *rpn_strerror(int code);

/* ------------------------------------------------------------------ */
/* Values                                                             */
/* ------------------------------------------------------------------ */

/* Opaque calculator handle (defined in Context below). */
typedef struct rpn_ctx_t rpn_ctx_t;

/*
 * Tag of a value.  Arithmetic promotes int -> real -> complex.  A complex
 * result is never demoted automatically (see the "demote" command).
 * RPN_T_VECTOR and RPN_T_MATRIX are reserved for a later stage and are
 * never produced by this release.
 */
typedef enum rpn_tag_t {
    RPN_T_INT,
    RPN_T_REAL,
    RPN_T_COMPLEX,
    RPN_T_VECTOR,
    RPN_T_MATRIX
} rpn_tag_t;

typedef struct rpn_value_t {
    rpn_tag_t tag;
    union {
        long i;
        double r;
        struct {
            double re;
            double im;
        } c;
    } u;
} rpn_value_t;

/* Constructors. */
rpn_value_t rpn_value_int(long i);
rpn_value_t rpn_value_real(double r);
rpn_value_t rpn_value_complex(double re, double im);

/*
 * Formats a value into buf (at least RPN_FORMAT_MAX bytes).
 * Ints print as decimal whatever the display mode.  Reals follow the
 * display mode of the calculator c (fix n, sci n, eng n, or all, the
 * default: 12 significant digits, always containing '.' or 'e' so that
 * the text reads back as a real).  Complex values print as "(re,im)".
 * c may be NULL, which selects the default mode.
 * Returns 0 on success, -1 if the buffer is too small or the value is
 * of an unsupported tag.
 */
int rpn_format(const rpn_ctx_t *c, const rpn_value_t *v, char *buf,
               size_t bufsz);

/*
 * Decides whether a token is a numeric literal: an int, a real, or a
 * complex written "(re,im)" with no spaces, each part an int or a real.
 *   returns  1  and fills *out   for a valid literal,
 *            0                   if the token is not a number,
 *           -1                   if it looks like a number but is not
 *                                representable (for example 1e999).
 * The token must already be lower case (rpn_next_token does that).
 */
int rpn_parse_number(const char *token, rpn_value_t *out);

/* ------------------------------------------------------------------ */
/* Host environment (IoC)                                             */
/* ------------------------------------------------------------------ */

/*
 * Every function pointer receives the host's `user` pointer.
 * alloc and release are mandatory.  out and error may be NULL.
 *
 *   alloc    return `size` bytes of storage or NULL.
 *   release  free storage returned by alloc.
 *   out      text output (used by commands that print).
 *   error    called once for every failed rpn_exec() with the error code
 *            and a message such as "stack underflow (swap)".
 */
typedef struct rpn_env_t {
    void *user;
    void *(*alloc)(void *user, size_t size);
    void (*release)(void *user, void *ptr);
    void (*out)(void *user, const char *text);
    void (*error)(void *user, int code, const char *message);
} rpn_env_t;

/* ------------------------------------------------------------------ */
/* Context                                                            */
/* ------------------------------------------------------------------ */

/* Creates a calculator.  The env structure is copied.  NULL on failure. */
rpn_ctx_t *rpn_new(const rpn_env_t *env);

/* Destroys a calculator and releases all its memory.  NULL is allowed. */
void rpn_free(rpn_ctx_t *c);

/*
 * Executes the commands in `text` (any number of lines; '#' starts a
 * comment that runs to the end of the line).  Execution stops at the first
 * error, which is returned and reported through env->error.  Values already
 * on the stack are kept; the failing command leaves the stack unchanged.
 */
int rpn_exec(rpn_ctx_t *c, const char *text);

/* Error code and message of the most recent rpn_exec() (RPN_OK / ""). */
int rpn_last_error(const rpn_ctx_t *c);
const char *rpn_last_error_message(const rpn_ctx_t *c);

/* ------------------------------------------------------------------ */
/* Stack access                                                       */
/* ------------------------------------------------------------------ */

/* Sets the maximum depth (>= 1 and >= current depth, else RPN_E_BAD_ARG). */
int rpn_set_stack_max(rpn_ctx_t *c, size_t max);
size_t rpn_stack_max(const rpn_ctx_t *c);

/* Number of values on the stack. */
size_t rpn_depth(const rpn_ctx_t *c);

/*
 * Reads the value at stack level `level` (0 = top) without changing the
 * stack.  Returns RPN_E_STACK_UNDERFLOW if level >= depth.
 */
int rpn_peek(const rpn_ctx_t *c, size_t level, rpn_value_t *out);

/* Pushes a value (int, or real/complex with finite parts). */
int rpn_push(rpn_ctx_t *c, const rpn_value_t *v);

/* ------------------------------------------------------------------ */
/* Programs                                                           */
/* ------------------------------------------------------------------ */

/*
 * Returns 1 while a block ("program { ... }" or "function { ... }") or a
 * command's { group } is open: the tokens given to rpn_exec() are then
 * stored or collected, not executed.  A front end can use it to suppress
 * its echo.
 */
int rpn_in_program(const rpn_ctx_t *c);

/* ------------------------------------------------------------------ */
/* Lexer                                                              */
/* ------------------------------------------------------------------ */

/*
 * Extracts the next token from *pp, advancing *pp.  Tokens are separated
 * by white space, are converted to lower case, and '#' starts a comment
 * that runs to the end of the line.  buf must hold at least bufsz bytes
 * (use RPN_MAX_TOKEN + 1).
 *   returns  1  token stored in buf,
 *            0  end of text,
 *           -1  token longer than bufsz - 1 (it is skipped; buf holds
 *               its truncated start).
 */
int rpn_next_token(const char **pp, char *buf, size_t bufsz);

#ifdef __cplusplus
}
#endif

#endif /* RPN_H */
