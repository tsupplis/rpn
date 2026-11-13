# Building and testing

## Targets

| Command | Result | Compiler |
|---|---|---|
| `make` | all three below | |
| `make native` | `native/rpn`, `native/librpn.a` | `cc -ansi -pedantic -Wall -Wextra -Werror` |
| `make cpm86` | `cpm86/rpn.cmd` | Aztec C 4.3 (`aztec43_acc`) |
| `make dos` | `dos/rpn.exe` | Aztec C 4.3 |
| `make test` | unit tests and scenarios | strict ANSI, then sanitizers |
| `make clean` | removes `native/` `cpm86/` `dos/` | |

The Makefile works with GNU make and BSD make (no `%` rules, no wildcards).

## Tests

| Part | What it runs |
|---|---|
| `tests/unit.c` | unit checks against the library, with allocation-failure injection |
| `tests/cases/*.rpn` | scenarios: the CLI is fed the file, output is compared with `name.rsp`; `name.rc` holds the expected exit status, `name.args` the options (`@` stands for the cases directory) |
| `examples/*.rpn` | run like scenarios, with `name.rsp` next to them |
| `tests/fuzz.c` | `make fuzz` (in `tests/`, after `make san`): mutated scenarios and random text under the sanitizers; checks the stack bound and that all memory is released |
| `tests/emu_cmp.sh` | compares native with an Aztec build under emu2 |

| Flavour | Flags |
|---|---|
| `ansi` | `-ansi -pedantic -Wall -Wextra -Werror -O1 -g` |
| `san` | `-std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize-recover=undefined` |

`UPDATE=1 sh run_test.sh ../native/rpn` (in `tests/`) rewrites the `.rsp` files. Check every one by hand before keeping it.

## Aztec C 4.3 (CP/M-86 and DOS)

| Topic | Rule |
|---|---|
| floating point | one binary per target, linked with `-lm87s`: it uses an 8087 when present, software otherwise |
| objects | built once in `cpm86/`, shared by both targets |
| paths | `aztec43_acc` only accepts paths below the current directory |
| librarian | needs an existing empty file and `-a+`; modules go in dependency order (single pass) |
| command line | DOS limits it to 127 characters, so the library is built in three librarian calls |
| `int` | 32 bits |
| overflow | reals saturate to `1.79e308` (`RPN_BIG` in `compat.h`) instead of infinity |
| const pointers | compare with `!ptr`, not `== NULL` (otherwise warning 60) |
| 64 KB data segment | about 1024 values fit on the stack (a value is 18 bytes) |
| `long` parameters | write `0L` / `1L` |

## Running under emu2

| Item | Rule |
|---|---|
| command | `emu2 cpm86/rpn.cmd -x file` (use `timeout`) |
| files | emu2 cannot see `/tmp`; copy the file under the project |
| stdin | `rpn.cmd` never sees end of file on redirected input: use `-x` or `-l` with a file name |
| command line | CP/M-86 upper-cases it (the `bad_opt` scenario differs by design) |
| comparing | strip `\r`, drop blank lines, `RPN:` becomes `rpn:`, sort both sides; `tests/emu_cmp.sh cpm86/rpn.cmd` does it |
| not compared | `cont_eof`, `overflow`, `bad_opt`, `join_ok` |
| FPU | emu2 has none: the software path runs, the 8087 path is checked by the link only |
