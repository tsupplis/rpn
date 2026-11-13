# Design note: vectors and matrices (later stage)

Status: **design only, nothing is implemented.** The tags `RPN_T_VECTOR` and `RPN_T_MATRIX` exist in `rpn.h`; no command produces them and `rpn_push` refuses them with `RPN_E_TYPE`.

## What the code already guarantees

| Fact | Where | Consequence |
|---|---|---|
| every math command starts with `rpn_is_num` | `arith.c`, `mathfn.c`, `cond.c` | a vector or matrix on the stack gives `RPN_E_TYPE` today, never a wrong result |
| values are copied by assignment (`rpn_value_t` is plain data) | `stack.c`, `vars.c` | a value that owns memory needs a new rule: see *Storage* |
| one allocator, supplied by the host | `rpn_env_t` | element storage must use it |
| a failing command leaves the stack unchanged | `rpn_run_cmd` | keep: compute the result first, store last |

## Storage

The problem: `rpn_value_t` is copied freely (`dup`, `lastx`, `store`, `recall`, locals, frames). A value that points to heap storage would be freed twice or leak.

| Option | Idea | Cost |
|---|---|---|
| **A. reference counted block** (proposed) | the value holds a pointer to a block `{count, rows, cols, doubles[]}`; copies increment, drops decrement | every place that copies or discards a value must call `retain` / `release` |
| B. copy on every copy | deep copy in `dup`, `store`, `recall` | simple, but a 100x100 matrix is copied by each `dup` |
| C. fixed small size inside the value | for example up to 4 elements | no allocation, but a hard limit |

Proposal: **A**, with the retain/release calls gathered in three helpers (`rpn_val_copy`, `rpn_val_drop`, `rpn_val_move`) and every direct assignment of a stack entry replaced by them. A test that counts allocations against releases (the mock environment already does) catches mistakes.

## Types and promotion

| Operand | With scalar | With vector | With matrix |
|---|---|---|---|
| scalar | as today | scales each element | scales each element |
| vector | scales | element-wise `+ -`; `*` is the dot product | matrix times vector when sizes agree |
| matrix | scales | matrix times vector | `+ -` element-wise, `*` matrix product |

| Rule | Detail |
|---|---|
| element type | `real` or `complex`; ints are promoted to real inside a vector or matrix |
| a vector is a column | `n x 1`; its transpose is `1 x n` and is a matrix |
| size mismatch | `RPN_E_BAD_ARG` |
| `lastx` | keeps the block like any other value |

## Literals and constructors

Braces now mean "text in a group", so vectors and matrices use another bracket. Square brackets are free and mean "a sequence of numbers":

| Form | Value |
|---|---|
| `[ 1 2 3 ]` | vector of 3 |
| `[ [ 1 2 ] [ 3 4 ] ]` | 2 x 2 matrix, row by row |
| `3 vector` | pops 3 values into a vector (last value last) |
| `2 3 matrix` | pops `2 x 3` values into a matrix, row by row |

`[` and `]` would be tokens of their own, like `{` and `}`. Inside a program they are stored as tokens and executed as constructors.

## Planned commands

| Command | Effect |
|---|---|
| `dot` | dot product of two vectors |
| `cross` | cross product, 3-vectors |
| `transpose` | `m` → `mᵀ` |
| `det` `inverse` | square matrices; singular gives `RPN_E_DOMAIN` |
| `solve` | `A b` → `x` for `A x = b` |
| `rows` `cols` `size` | dimensions as ints |
| `at` | `m i j` → element; `v i` → element |
| `identity` | `n` → identity matrix |
| `norm` | vector length, or Frobenius norm of a matrix |
| `unpack` | puts the elements back on the stack, then the size |

Existing one-argument math functions (`sqrt`, `sin`, `neg`, ...) would map over the elements; this is added per function, not all at once.

## Display and tests

| Item | Plan |
|---|---|
| `rpn_format` | `[1 2 3]`, `[[1 2][3 4]]`; the buffer of `RPN_FORMAT_MAX` is too small, so a sized variant is added |
| CLI echo | one line, elided after a width limit |
| tests | allocation balance for every command; results checked by hand against small cases; mismatch and singular cases for every error |
| limits | a maximum element count (configurable), so a script cannot exhaust memory |

## Not planned

Sparse matrices, integer or exact-rational matrices, eigenvalues, decompositions beyond what `solve` needs, and higher-rank tensors.

## Order of work

| Step | Content |
|---|---|
| 1 | retain/release helpers and the audit of every copy site, with the current types only (no behaviour change) |
| 2 | vector type: literal, `vector`, display, `dot`, element-wise arithmetic |
| 3 | matrix type: literal, `matrix`, product, `transpose`, `det`, `inverse`, `solve` |
| 4 | mapping of the scalar functions; documentation and examples |
