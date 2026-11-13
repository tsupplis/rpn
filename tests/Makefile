# tests/Makefile - runs the test suite (GNU make and BSD make).
#
#   make        run both flavours (ansi, then san)
#   make ansi   strict ANSI C89 build
#   make san    C11 build with address and undefined-behaviour sanitizers
#   make fuzz   (after make san) mutated scripts and random text
#   make clean
#
# Each flavour rebuilds the library and the CLI in ../native with its own
# flags, then runs native/unit (unit.c) and run_test.sh against them.

CC     = cc
ANSI   = -ansi -pedantic -Wall -Wextra -Werror -O1 -g
SAN    = -std=c11 -Wall -Wextra -Werror -Wno-deprecated-declarations -g -fsanitize=address,undefined -fno-sanitize-recover=undefined

all: ansi san

ansi:
	rm -rf ../native && cd .. && $(MAKE) CFLAGS="$(ANSI)"
	$(MAKE) check CFLAGS="$(ANSI)"

san:
	rm -rf ../native && cd .. && $(MAKE) CFLAGS="$(SAN)"
	$(MAKE) check CFLAGS="$(SAN)"

check: ../native/unit
	../native/unit
	sh run_test.sh ../native/rpn

# fuzz: mutated scenarios and examples under the current flags (use san)
fuzz: ../native/fuzz
	../native/fuzz cases/*.rpn ../examples/*.rpn

../native/fuzz: fuzz.c ../rpn.h ../native/librpn.a
	$(CC) $(CFLAGS) -I.. -o ../native/fuzz fuzz.c ../native/librpn.a -lm

../native/unit: unit.c ../rpn.h ../native/librpn.a
	$(CC) $(CFLAGS) -I.. -o ../native/unit unit.c ../native/librpn.a -lm

clean:
	rm -rf ../native/unit ../native/fuzz

.PHONY: all ansi san check fuzz clean
