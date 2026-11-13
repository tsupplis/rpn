# librpn API

ANSI C89, no global state. The host supplies memory, output and error reporting. Include `rpn.h`, link `librpn.a`.

```c
rpn_env_t env;                        /* filled in by the host */
rpn_ctx_t *c = rpn_new(&env);
if (rpn_exec(c, "3 4 + 2 *") == RPN_OK) {
    rpn_value_t v;
    char buf[RPN_FORMAT_MAX];
    rpn_peek(c, 0, &v);
    rpn_format(c, &v, buf, sizeof buf);   /* "14" */
}
rpn_free(c);
```

## Host environment

`rpn_env_t` is copied by `rpn_new`. Every function receives `user`.

| Member | Required | Called for |
|---|---|---|
| `alloc(user, size)` | yes | all storage; return `NULL` on failure |
| `release(user, ptr)` | yes | freeing what `alloc` returned |
| `out(user, text)` | no | commands that print (`view`) |
| `error(user, code, message)` | no | once per failed `rpn_exec`, e.g. `stack underflow (swap)` |

## Functions

| Function | Purpose |
|---|---|
| `rpn_new(env)` / `rpn_free(c)` | create / destroy (`NULL` is allowed for free) |
| `rpn_exec(c, text)` | run any number of lines; stops at the first error and returns its code |
| `rpn_last_error(c)` / `rpn_last_error_message(c)` | code and message of the last `rpn_exec` |
| `rpn_strerror(code)` | static description of a code |
| `rpn_depth(c)` | number of values on the stack |
| `rpn_peek(c, level, &v)` | read level `level` (0 = top) without changing the stack |
| `rpn_push(c, &v)` | push a value |
| `rpn_set_stack_max(c, n)` / `rpn_stack_max(c)` | stack limit (default `RPN_DEFAULT_STACK_MAX`) |
| `rpn_in_program(c)` | 1 while a block or a `{ group }` is open (a front end should not echo) |
| `rpn_value_int` / `_real` / `_complex` | value constructors |
| `rpn_format(c, &v, buf, size)` | text of a value, following the display mode; `buf` needs `RPN_FORMAT_MAX` bytes |
| `rpn_parse_number(token, &v)` | 1 number, 0 not a number, -1 not representable |
| `rpn_next_token(&p, buf, size)` | lexer: 1 token, 0 end of text, -1 too long |

## Values

| Tag | Member | Note |
|---|---|---|
| `RPN_T_INT` | `u.i` (`long`) | exact |
| `RPN_T_REAL` | `u.r` (`double`) | finite |
| `RPN_T_COMPLEX` | `u.c.re`, `u.c.im` | printed `(re,im)` |
| `RPN_T_VECTOR`, `RPN_T_MATRIX` | | reserved, never produced |

## Limits

| Constant | Value | Meaning |
|---|---|---|
| `RPN_MAX_TOKEN` | 64 | longest token; buffers need one more byte |
| `RPN_DEFAULT_STACK_MAX` | 100 | stack depth until changed |
| `RPN_DEFAULT_STEP_LIMIT` | 1000000 | tokens per run (`steplimit` changes it) |
| `RPN_DEFAULT_CALL_LIMIT` | 256 | nested calls (`calllimit` changes it) |
| `RPN_FORMAT_MAX` | 96 | minimum buffer for `rpn_format` |

## Error codes

`RPN_OK` is 0. A failing `rpn_exec` leaves the stack unchanged.

| Code | Meaning |
|---|---|
| `RPN_E_NOMEM` | `alloc` returned `NULL` |
| `RPN_E_STACK_OVERFLOW` / `_UNDERFLOW` | beyond the maximum / too few values |
| `RPN_E_DIV_ZERO` | division, `mod`, `inv` or `0^-n` by zero |
| `RPN_E_INT_OVERFLOW` / `RPN_E_REAL_OVERFLOW` | result does not fit |
| `RPN_E_DOMAIN` | argument outside the domain |
| `RPN_E_TYPE` | wrong value type |
| `RPN_E_BAD_ARG` | invalid argument, or wrong `{ group }` shape |
| `RPN_E_UNKNOWN_CMD` / `RPN_E_TOKEN_TOO_LONG` | bad token |
| `RPN_E_BAD_NAME` / `RPN_E_UNDEFINED` / `RPN_E_DUPLICATE` | names, labels, locals and functions |
| `RPN_E_PROGRAM` | not valid here |
| `RPN_E_CALL_DEPTH` / `RPN_E_STEP_LIMIT` | run limits |
| `RPN_E_SCOPE` | label exists in another scope |

## Not in the library

Line editing, `_` continuation, the 256-character line limit, the echo and the `p` pseudo-operator belong to the CLI (`main.c`).
