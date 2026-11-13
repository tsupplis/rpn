#!/bin/sh
# emu_cmp.sh - compares every scenario between native and an Aztec build
# under emu2.  usage: sh tests/emu_cmp.sh cpm86/rpn.cmd  (or dos/rpn.exe)
# Normalised as in the plan: no \r, no blank lines, RPN: -> rpn:, sorted.
E=/Users/thierry/.local.darwin/emulators/cpm86-crossdev/bin/emu2
BIN=$1
DIR=tests/cases
norm() { tr -d '\r' | grep -v '^$' | sed 's/^RPN:/rpn:/' | sort; }
same=0; diff_=0
for in in $DIR/*.rpn examples/*.rpn; do
    name=`basename $in .rpn`; d=`dirname $in`
    # differ by design: stdin EOF under emu2, 16-bit overflow, the upper-casing
    # of the command line on CP/M-86, and the 64 KB data segment (join_ok)
    case $name in cont_eof|overflow|bad_opt|join_ok) continue;; esac
    args=""
    [ -f $d/$name.args ] && args=`sed "s#@#$DIR/#g" $d/$name.args`
    case "$args" in *-[xlLX]*|*@*) continue;; esac
    cp $in emu_in.rpn
    ./native/rpn $args -x emu_in.rpn 2>&1 | norm > emu_nat.txt
    timeout 60 $E $BIN $args -x emu_in.rpn 2>&1 | norm > emu_az.txt
    if cmp -s emu_nat.txt emu_az.txt; then same=`expr $same + 1`
    else diff_=`expr $diff_ + 1`; echo "DIFF $name"; diff emu_nat.txt emu_az.txt | head -6; fi
done
rm -f emu_in.rpn emu_nat.txt emu_az.txt
echo "$BIN: $same same, $diff_ different"
