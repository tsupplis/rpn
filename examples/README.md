# Examples

Run one with `rpn examples/name.rpn`. Each has a `name.rsp` with the expected output, and `make test` runs them all.

| File | Shows | Result to expect |
|---|---|---|
| `fibonacci.rpn` | `function`, `local`, parallel assignment, a loop, recursion | `fibonacci` of 40 is `102334155` |
| `factorial.rpn` | loop and recursion side by side, exact ints | `12 factorial` is `479001600` |
| `primes.rpn` | a function calling a function, results left on the stack | primes up to 50, largest `47` |
| `gcd.rpn` | Euclid with a parallel assignment, composing functions | `1071 462 gcd` is `21` |
| `newton.rpn` | reals, a convergence test | `2 newton_sqrt` is `1.41421356237` |
| `quadratic.rpn` | complex roots with no special case | `1 2 5 quadratic` gives `(-1,2)` and `(-1,-2)` |
| `collatz.rpn` | if / else with a skipped `goto`, an unbounded loop | `27 collatz` is `111` steps |

Conventions used in all of them:

| Convention | Why |
|---|---|
| `local { … }` for the first binding, `store { … }` for later updates | `store` on a name that is not a local would create a global |
| a test is followed by a `goto { … }`, and both paths `drop` the tested value | tests never change the stack |
| the comment `# stack: a b -> …` after a test | shows which values are compared (`x` is the top) |
