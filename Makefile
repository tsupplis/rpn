# Makefile - builds librpn and the rpn CLI (GNU make and BSD make).
#
#   make          native build           -> native/rpn
#   make cpm86    Aztec C 4.3, CP/M-86   -> cpm86/rpn.cmd
#   make dos      Aztec C 4.3, MS-DOS    -> dos/rpn.exe
#   make test     run the tests (see tests/Makefile)
#   make clean    remove native/, cpm86/ and dos/
#
# Rules are spelled out per file (no % rules, no wildcards) so that the
# same file works with GNU make and BSD make.

CC      = cc
CFLAGS  = -ansi -pedantic -Wall -Wextra -Werror -O1 -g
LDLIBS  = -lm

NATIVE  = native
CPM86   = cpm86
DOS     = dos

AZCC    = aztec43_acc
AZAR    = aztec43_lib
CMDINFO = pcdev_cmdinfo

HEADERS = rpn.h intern.h compat.h

# ---- native ---------------------------------------------------------

NOBJ    = $(NATIVE)/arith.o $(NATIVE)/cmd.o $(NATIVE)/complex.o $(NATIVE)/cond.o $(NATIVE)/ctx.o $(NATIVE)/lexer.o $(NATIVE)/mathfn.o $(NATIVE)/prog.o $(NATIVE)/stack.o $(NATIVE)/value.o $(NATIVE)/vars.o

native: $(NATIVE)/rpn 

all: native cpm86 dos

$(NATIVE)/rpn: $(NATIVE)/main.o $(NATIVE)/librpn.a
	$(CC) $(CFLAGS) -o $@ $(NATIVE)/main.o $(NATIVE)/librpn.a $(LDLIBS)

$(NATIVE)/librpn.a: $(NOBJ)
	rm -f $@
	ar rcs $@ $(NOBJ)

$(NATIVE)/main.o: main.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c main.c -o $@
$(NATIVE)/arith.o: arith.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c arith.c -o $@
$(NATIVE)/cmd.o: cmd.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c cmd.c -o $@
$(NATIVE)/complex.o: complex.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c complex.c -o $@
$(NATIVE)/ctx.o: ctx.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c ctx.c -o $@
$(NATIVE)/lexer.o: lexer.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c lexer.c -o $@
$(NATIVE)/mathfn.o: mathfn.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c mathfn.c -o $@
$(NATIVE)/cond.o: cond.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c cond.c -o $@
$(NATIVE)/prog.o: prog.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c prog.c -o $@
$(NATIVE)/vars.o: vars.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c vars.c -o $@
$(NATIVE)/stack.o: stack.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c stack.c -o $@
$(NATIVE)/value.o: value.c $(HEADERS)
	@mkdir -p $(NATIVE)
	$(CC) $(CFLAGS) -c value.c -o $@

# ---- Aztec C 4.3: CP/M-86 and MS-DOS -------------------------------
# The compiler, the librarian and the objects are shared: the objects are
# built once in cpm86/.  Each target then creates its own library and does
# its own link.  Sharing objects is safe because no source depends on
# __CPM86__ or __MSDOS__.  The link step takes the target OS from the
# suffix of the output (.cmd or .exe).
#
# aztec43_acc only accepts paths below the current directory.  The Aztec
# librarian cannot create a library: it needs an existing (empty) file,
# and with a plain -a it reads the first module name as a position, so -a+
# (append) is used instead.  Modules are added in dependency order, as the
# librarian makes a single pass.
#
# One binary per target, linked with -lm87s: the start-up code probes for
# an 8087 (or a later FPU) and uses it when present, otherwise the floating
# point runs in software.  There is no separate "87" build.  emu2 has no
# FPU, so under it the software path runs; the 8087 path is only checked by
# the link.  (-lm87 would require an FPU and exit with "8087/80287 is
# required!" without one.)
#
# Known emu2 limitation (not a bug in rpn): under emu2, rpn.cmd never sees
# end of file on redirected stdin (getc keeps returning '\n') and its exit
# status is always 0.  End the input with a Ctrl-Z byte, or use -x/-l with
# a file name.  dos/rpn.exe is not affected.

# Referencing modules come before the modules they use (single pass).
# The DOS command line is limited to 127 characters, so the librarian is
# called three times, keeping that order.
AZOBJ   = $(AZOBJ1) $(AZOBJ2) $(AZOBJ3)
AZOBJ1  = $(CPM86)/ctx.o $(CPM86)/prog.o $(CPM86)/cmd.o $(CPM86)/vars.o
AZOBJ2  = $(CPM86)/cond.o $(CPM86)/mathfn.o $(CPM86)/arith.o $(CPM86)/stack.o
AZOBJ3  = $(CPM86)/complex.o $(CPM86)/value.o $(CPM86)/lexer.o

cpm86: $(CPM86)/rpn.cmd

dos: $(DOS)/rpn.exe

$(CPM86)/rpn.cmd: $(CPM86)/main.o $(CPM86)/rpn.lib
	$(AZCC) -o $@ $(CPM86)/main.o $(CPM86)/rpn.lib -lm87s
	$(CMDINFO) $@

$(DOS)/rpn.exe: $(CPM86)/main.o $(DOS)/rpn.lib
	$(AZCC) -o $@ $(CPM86)/main.o $(DOS)/rpn.lib -lm87s

$(CPM86)/rpn.lib: $(AZOBJ) Makefile
	: > $@
	$(AZAR) $@ -a+ $(AZOBJ1)
	$(AZAR) $@ -a+ $(AZOBJ2)
	$(AZAR) $@ -a+ $(AZOBJ3)

$(DOS)/rpn.lib: $(AZOBJ) Makefile
	@mkdir -p $(DOS)
	: > $@
	$(AZAR) $@ -a+ $(AZOBJ1)
	$(AZAR) $@ -a+ $(AZOBJ2)
	$(AZAR) $@ -a+ $(AZOBJ3)

$(CPM86)/main.o: main.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ main.c
$(CPM86)/arith.o: arith.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ arith.c
$(CPM86)/cmd.o: cmd.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ cmd.c
$(CPM86)/complex.o: complex.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ complex.c
$(CPM86)/ctx.o: ctx.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ ctx.c
$(CPM86)/lexer.o: lexer.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ lexer.c
$(CPM86)/mathfn.o: mathfn.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ mathfn.c
$(CPM86)/cond.o: cond.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ cond.c
$(CPM86)/prog.o: prog.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ prog.c
$(CPM86)/vars.o: vars.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ vars.c
$(CPM86)/stack.o: stack.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ stack.c
$(CPM86)/value.o: value.c $(HEADERS)
	@mkdir -p $(CPM86)
	$(AZCC) -c -o $@ value.c

test:
	cd tests && $(MAKE)

clean:
	rm -rf $(NATIVE) $(CPM86) $(DOS)

.PHONY: all native cpm86 dos test clean
