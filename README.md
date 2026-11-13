# rpn

A small RPN calculator and programming language written in C.

- Interactive command-line REPL and script execution
- Integer, real, and complex arithmetic
- Stack operations, variables, functions, and control flow
- Embeddable C library (`librpn`)

## Example

The [GCD example](examples/gcd.rpn) uses Euclid's algorithm:

```rpn
function { gcd
  local { a b }
  label { again }
  b x=0? goto { done }
  drop
  b a b mod local { a b }
  goto { again }
  label { done }
  drop
  a
}

1071 462 gcd  # 21
```
