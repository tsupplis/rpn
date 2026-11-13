#!/bin/sh
# run_test.sh - test runner.
#
# usage: sh run_test.sh path/to/rpn
#
# For every tests/cases/NAME.rpn the CLI is run with NAME.rpn on standard
# input (and the options in NAME.args, if that file exists).  Standard
# output and standard error are captured together and compared with
# NAME.out.  In NAME.args an "@" stands for the cases directory, so that
# options such as "-l @defs.inc" can name fixture files (*.inc).
# If NAME.rc exists it holds the expected exit status (default 0).
# The programs in ../examples are run the same way, with NAME.out next to
# them, so that the examples cannot rot.
# Run "UPDATE=1 sh run_test.sh rpn" to rewrite the .out files; review the diff.

RPN=${1:-../rpn}
DIR=`dirname "$0"`/cases
EXDIR=`dirname "$0"`/../examples
pass=0
fail=0

for in in "$DIR"/*.rpn "$EXDIR"/*.rpn; do
    name=`basename "$in" .rpn`
    dir=`dirname "$in"`
    args=""
    [ -f "$dir/$name.args" ] && args=`sed "s#@#$DIR/#g" "$dir/$name.args"`
    want_rc=0
    [ -f "$dir/$name.rc" ] && want_rc=`cat "$dir/$name.rc"`
    "$RPN" $args < "$in" > "$dir/$name.actual" 2>&1
    rc=$?
    if [ -n "$UPDATE" ]; then
        cp "$dir/$name.actual" "$dir/$name.out"
    fi
    if cmp -s "$dir/$name.actual" "$dir/$name.out" && [ "$rc" = "$want_rc" ]; then
        pass=`expr $pass + 1`
        rm -f "$dir/$name.actual"
    else
        fail=`expr $fail + 1`
        echo "FAIL: $name (exit $rc, expected $want_rc)"
        diff "$dir/$name.out" "$dir/$name.actual" | head -20
    fi
done

echo "tests: $pass passed, $fail failed"
[ "$fail" = 0 ]
