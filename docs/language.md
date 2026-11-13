# rpn language reference

Text is a stream of **atoms** separated by white space. Input is case-insensitive. `#` starts a comment that runs to the end of the line. Line breaks do not matter, except for the CLI rules in [Command line](#command-line).

## Atoms

| Atom | Example | Effect |
|---|---|---|
| number | `3` `-2.5e-1` `(1,2)` | pushed on the stack |
| command | `+` `swap` `sqrt` | runs on the stack |
| command + group | `store { x }` `goto { loop }` | one atom: the command reads its group |
| variable name | `x` | pushes the value of `x` |
| function name | `hyp` | calls the function |

**Rule:** what is written in the text goes in `{ }` after its command. What is computed comes from the stack.

| Written | Computed |
|---|---|
| `store { x }` | `10 3 /` |
| `setflag { 3 }` | `4 fix` |
| `goto { loop }` | `100 steplimit` |

`{` and `}` are always their own tokens, so `goto{loop}` works. A group may span lines.

## Numbers

| Kind | Form | Examples | Note |
|---|---|---|---|
| int | `[+-]digits` | `42` `-7` | exact, `long`; overflow is an error |
| real | digits with `.` or `e` | `2.5` `.5` `5.` `1e3` | double |
| complex | `(re,im)` | `(1,2)` `(1.5,-2)` | no spaces |

Arithmetic promotes **int → real → complex**. A complex result is never demoted on its own: use `demote`.
Integer division stays an int when it is exact: `6 3 /` is `2`, `7 2 /` is `3.5`.

## Stack

Grows up to a maximum (default 100, `-s N` on the command line). There is no register duplication on drop. Overflow and underflow are errors, and a failing command leaves the stack unchanged. `lastx` returns the top value from before the last arithmetic command.

| Command | Alias | Effect (top on the right) |
|---|---|---|
| `dup` | `enter` | `a` → `a a` |
| `drop` | | `a` → |
| `swap` | `x<>y` | `a b` → `b a` |
| `rolldown` | `rdn` | `a b c d` → `d a b c` |
| `rollup` | `rup` | `a b c d` → `b c d a` |
| `clear` | `clx` | empties the stack |
| `lastx` | | pushes the value of the top before the last command |

## Arithmetic

| Command | Alias | Stack effect | Note |
|---|---|---|---|
| `+` `-` `*` `/` | | `y x` → `y op x` | `/` by zero is an error |
| `mod` | | `y x` → `y mod x` | result has the sign of `x`: `-7 3 mod` is `2` |
| `pow` | `y^x` | `y x` → `y^x` | int when exact |
| `percent` | `%` | `y x` → `y`, `x% of y` | keeps `y`: `200 15 percent` leaves `200 30` |
| `neg` | `chs` | `x` → `-x` | |
| `inv` | `1/x` | `x` → `1/x` | |
| `abs` | | `x` → `|x|` | modulus of a complex |
| `sq` | `x^2` | `x` → `x²` | |
| `sqrt` | | `x` → `√x` | `-1 sqrt` is `(0.0,1.0)` unless `strict` |

## Functions

| Group | Commands |
|---|---|
| exponential, log | `exp` (`e^x`) `ln` `log` `log2` `ln1p` (`ln1+x`) `expm1` (`e^x-1`) `pow10` (`10^x`) `pow2` |
| trigonometric | `sin` `cos` `tan` `asin` `acos` `atan` `atan2` |
| hyperbolic | `sinh` `cosh` `tanh` `asinh` `acosh` `atanh` |
| constants | `pi` `e` |
| rounding | `floor` `ceil` `round` `trunc` `fraction` (`frac`) |
| complex | `re` `im` `conj` `arg` `complex` `polar` (`->pol`) `rect` (`->rec`) `demote` |

Each takes one value, except the constants (none) and these:

| Command | Stack effect |
|---|---|
| `atan2` | `y x` → `atan2(y, x)` |
| `complex` | `re im` → `(re,im)` |
| `polar` (`->pol`) | `y x` → `θ r` (the magnitude `r` ends on top) |
| `rect` (`->rec`) | `θ r` → `y x` (`x` ends on top) |

A real argument outside the real domain gives a complex result (`2 asin`), or a **domain error** in `strict` mode.

## Modes

| Command | Alias | Effect |
|---|---|---|
| `radians` `degrees` `grads` | `rad` `deg` `grad` | angle unit for real arguments of `sin` `cos` `tan` and real results of `asin` `acos` `atan` `atan2` `arg` `polar`. Complex values are always in radians |
| `fix n` `sci n` `eng n` | | display reals with `n` digits (`n` on the stack) |
| `all` | | default: 12 significant digits |
| `strict` `lenient` | | domain errors instead of complex results, or back |

Ints are never affected by the display mode.

## Tests

A test never changes the stack. When it is **false**, the next atom is skipped, together with its group.

| Test | True when |
|---|---|
| `x=0?` `x!=0?` `x<0?` `x>0?` `x<=0?` `x>=0?` | top compared with 0 |
| `x=y?` `x!=y?` `x<y?` `x>y?` `x<=y?` `x>=y?` | top (`x`) compared with level 1 (`y`) |
| `real?` `complex?` | top is an int or a real / a complex |
| `integer?` | top is an int, or a real with no fraction |
| `flag-set? { n }` `flag-clear? { n }` | flag `n` (0 to 31); aliases `fs?` `fc?` |

Only `=` and `!=` accept complex operands.

```
x=0?  goto { done }   # jumps only when x is 0
```

Because the tested value stays on the stack, **each path must drop it** (see `examples/`).

## Variables and flags

Names: `[a-z][a-z0-9_]*`, not a command word, not a function name.

| Command | Alias | Effect |
|---|---|---|
| `store { x }` | `sto` | pops the top into `x` |
| `recall { x }` | `rcl` | pushes `x` (same as writing `x`) |
| `store+ { x }` `store- { x }` `store* { x }` `store/ { x }` | `sto+` `sto-` `sto*` `sto/` | `x = x op top`, pops the top |
| `view { x }` | | prints `x = value` |
| `clearvars` | | deletes all variables |
| `setflag { n }` `clearflag { n }` | `sf` `cf` | flag `n`, 0 to 31 |
| `decrement-skip-zero { x }` | `dse` | `x = x - step`; skips the next atom if `x <= final` |
| `increment-skip-greater { x }` | `isg` | `x = x + step`; skips the next atom if `x > final` |

Loop counters use the HP-42S form `ccc.fffii`: counter, final value, step (`00` means 1). An int is a counter with final value 0 and step 1.

## Programs, labels and calls

| Command | Alias | Effect |
|---|---|---|
| `program { … }` | | stores the body; nothing runs while it is entered |
| `label { name }` | `lbl` | marks a place |
| `goto { name }` | `gto` | jumps to the label |
| `execute { name }` | `xeq` | calls the label as a subroutine |
| `return` | `rtn` | back to the caller; at the top of a run, ends it |
| `stop` | | ends the run |
| `list` | | prints the stored programs and functions as source (canonical words, aliases expanded); redirect it to a file and run that file again with `-l` |
| `clearprogram` | `clp` | deletes all programs and functions |
| `steplimit` | | `n` → sets the step limit (default 1 000 000 tokens) |
| `calllimit` | | `n` → sets the call depth limit (default 256) |

At the top level, `goto` and `execute` both **start a run** at the label. A run ends at `return`, `stop`, an error, a limit, or the end of the program text. A program body ends with an implicit `return`. `label`, `return` and `stop` outside a run are *not valid here*.

## Functions and locals

```
function { hyp
  local { a b }
  a sq b sq + sqrt
}
3 4 hyp                       # 5
```

| Item | Rule |
|---|---|
| `function { name body }` | defines a function; call it by writing `name` |
| `local { a b c }` | pops the stack into named locals; **the last name gets the top**: `1 2 3 local { a b c }` gives `a=1 b=2 c=3` |
| `local` on an existing local | overwrites it: `b a b + local { a b }` is a parallel assignment |
| `local` stack underflow | an error; the stack is unchanged |
| end of the function | `}` or `return` returns; locals are released |
| a call | has its own locals; a callee cannot see its caller's locals |
| `local` | valid only inside a function |
| name | not a command, not an existing function, not a global variable |
| redefinition | an error; `clearprogram` first |

**Locals and `store`.** Inside a function a name resolves to a local first, then to a global. `store { n }` updates a local named `n` if there is one. Otherwise it creates or updates a **global**. So: use `local` for the first binding of a name, then `store` (or `store+` …) for later updates.

**Label scope.**

| Label defined | Visible from |
|---|---|
| outside any function | global code only |
| inside `function { }` | that function only |

`goto` or `execute` towards a label in another scope is a **scope error**. The same label name may exist in different scopes. `execute { label }` inside a function is a plain call that shares the function's locals and scope.

## Command line

| Item | Rule |
|---|---|
| echo | after each line, the top of the stack is printed (nothing if empty, nothing while a program, function or group is open) |
| `p` | `p` or `0 p` top; `n p` level `n`; `a b p` levels `a` to `b`; `* p` all levels. The numbers before `p` are consumed. Not allowed inside a program or function |
| continuation | a line ending with `_` is joined to the next one. A physical line is at most 256 characters, a joined line 4096 |
| `exit` `quit` | end the run; the exit status is the top of the stack, 0 to 255 |

| Option | Effect |
|---|---|
| `-h` `-v` | help, version |
| `-q` | no echo |
| `-s N` | maximum stack depth (default 100) |
| `-l file` | load a file silently, then carry on |
| `-x file` | run a file, then stop (stdin is not read) |
| `file …` | run the files in order. With no file, or `-`, read standard input |

## Errors

Reported as `rpn: line N: message (what)`. A failing atom leaves the stack unchanged, and the rest of the line is dropped.

| Message | Cause |
|---|---|
| out of memory | the host allocator failed |
| stack overflow | beyond the stack maximum |
| stack underflow | not enough values |
| division by zero | `/`, `mod`, `inv`, `0^-n` |
| integer overflow | an int result does not fit a `long` |
| real overflow | a real result or literal is not finite |
| domain error | argument outside the domain (`strict`), `tan` of a quarter turn, a limit below 1 |
| wrong value type | for example ordering complex numbers, `2.5 steplimit` |
| bad argument | wrong group shape, flag out of range |
| unknown command | not a number, command, variable or function |
| token too long | more than 64 characters |
| invalid name | not a valid variable, function or label name |
| undefined name | no such variable or label |
| duplicate name | a label, a local or a function defined twice |
| not valid here | for example `return` outside a run, `local` outside a function, a block inside a block |
| call depth exceeded | calls nested beyond `calllimit` |
| step limit exceeded | a run longer than `steplimit` |
| label out of scope | the label exists, but in another scope |

**Group shape errors** (all *bad argument*): a missing group (`store x`), an empty group (`store { }`), a second item in a one-item group (`store { a b }`).
