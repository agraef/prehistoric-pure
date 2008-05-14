
# This Makefile requires GNU make. Really.

# Basic setup. You can change the version number and installation paths here.

# For instance, to install under /usr instead of /usr/local, run 'make
# prefix=/usr && make install prefix=/usr'. Please note that the 'prefix'
# option must be specified *both* at build and at installation time. At
# installation time, you can also specify a DESTDIR path if you want to
# install into a staging directory, e.g.: 'make install DESTDIR=$PWD/BUILD'.

version = 0.2
dist = pure-$(version)

prefix = /usr/local
bindir = $(prefix)/bin
libdir = $(prefix)/lib/pure
mandir = $(prefix)/share/man/man1

# Staging directory for 'make install'. If you use this, make sure that this
# ends in a slash.

DESTDIR=

# Linker flags. Adjust these as necessary for your system.

# LDFLAGS is for additional linker options, LIBS for additional libraries that
# might be needed (e.g., -liconv on OSX). Note that the -rdynamic flag is
# required when using gcc with the ELF linker (e.g., Linux on x86) to enable
# the dlopening of the program executable. On OSX this option isn't needed
# (pass LDFLAGS="" to 'make' instead).

LDFLAGS=-rdynamic
LIBS=

# Compilation flags. We currently provide the following build profiles:

# 'default' compiles with extra runtime checks and debugging information.
# 'release' optimizes for execution speed (release version).
# 'debug'   like 'default', but without any optimizations.
# 'debug2'  extensive debugging output, useful to debug the interpreter.

# The 'default' build should be suitable for most purposes. As of Pure 0.3,
# this build now also does basic optimizations and so isn't much slower than
# the 'release' build any more. In contrast to 'release', this build also
# provides diagnostics useful for maintainers if anything is wrong with the
# interpreter.

# The 'release' build disables all runtime checks and debugging information
# and compiles with additional optimizations which makes programs go a little
# faster (some 5-10% on a single-cpu AMD system running Linux and gcc 4.1,
# YMMV); use this if performance is critical.

# The 'debug' build is like 'default' but without any optimizations; it builds
# faster than 'default', but runs *much* slower, and isn't recommended for
# anything but debugging the interpreter. The 'debug2' build generates massive
# amounts of additional debugging messages, and is really only to be used by
# maintainers debugging the interpreter.

# To build with a given profile, just say 'make build=<profile>', e.g.: 'make
# build=release'. (This option only has to be specified at build time, not for
# installation or any other targets except 'all'.) Just 'make' builds with the
# default flags.

# Note that both the 'default' and the 'release' build may take quite a while
# to compile (especially runtime.cc), so please be patient. ;-)

build=default

LLVM_FLAGS = `llvm-config --cppflags`
LLVM_LIBS = `llvm-config --ldflags --libs core jit native`

# NOTE: Some of the following flags are gcc-specific, so you'll have to fiddle
# with the options if you're using a different compiler.

ifeq ($(build),default)
CXXFLAGS = -g -O -Wall $(LLVM_FLAGS)
CFLAGS = -g -O -Wall
else
ifeq ($(build),release)
CXXFLAGS = -O3 -DNDEBUG -Wall $(LLVM_FLAGS)
CFLAGS = -O3 -DNDEBUG -Wall
else
ifeq ($(build),debug)
CXXFLAGS = -g -Wall $(LLVM_FLAGS)
CFLAGS = -g -Wall
else
ifeq ($(build),debug2)
CXXFLAGS = -g -Wall -DDEBUG=2 $(LLVM_FLAGS)
CFLAGS = -g -Wall -DDEBUG=2
else
CXXFLAGS = -g -O -Wall $(LLVM_FLAGS)
CFLAGS = -g -O -Wall
.PHONY: warn
warn: all
	@echo "WARNING: Invalid build profile '$(build)'."
	@echo "WARNING: Must be one of 'default', 'release', 'debug' and 'debug2'."
	@echo "WARNING: Assuming 'default' profile."
endif
endif
endif
endif

# No need to edit below this line. Unless you really have to. :) ############

SOURCE = expr.cc expr.hh funcall.h interpreter.cc interpreter.hh lexer.ll \
matcher.cc matcher.hh parser.yy printer.cc printer.hh pure.cc \
runtime.cc runtime.h symtable.cc symtable.hh util.cc util.hh
EXTRA_SOURCE = lexer.cc parser.cc parser.hh location.hh position.hh stack.hh
OBJECT = $(subst .cc,.o,$(filter %.cc,$(SOURCE) $(EXTRA_SOURCE)))
ALL_LIBS = $(LLVM_LIBS) -lreadline -lgmp $(LIBS)

examples = $(wildcard examples/*.pure)
lib = $(wildcard lib/*.pure)
tests = $(wildcard test/*.pure)
logs = test/prelude.log $(tests:.pure=.log)
distlogs = $(wildcard test/*.log)

DISTFILES = COPYING ChangeLog INSTALL NEWS README TODO Makefile \
$(SOURCE) $(EXTRA_SOURCE) w3centities.c pure.1 pure.xml pure.vim \
$(examples) $(lib) $(tests) $(distlogs)

.PHONY: all html dvi ps pdf clean realclean depend install uninstall strip \
dist distcheck cleanlogs logs check

# compilation

all: pure

pure: $(OBJECT)
	$(CXX) -o $@ $(LDFLAGS) $(OBJECT) $(ALL_LIBS)

pure.o: pure.cc
	$(CXX) $(CXXFLAGS) -DVERSION='"$(version)"' -DPURELIB='"$(libdir)"' -c -o $@ $<

lexer.cc: lexer.ll
	flex -o lexer.cc $<

parser.cc: parser.yy
	bison -v -o parser.cc $<

parser.hh location.hh position.hh stack.hh: parser.cc

# documentation in various formats (requires groff)

html: pure.html
dvi: pure.dvi
ps: pure.ps
pdf: pure.pdf

%.html: %.1
	groff -man -Thtml $< > $@

%.dvi: %.1
	groff -man -Tdvi $< > $@

%.ps: %.1
	groff -man -Tps $< > $@

%.pdf: %.1
	groff -man -Tps $< | ps2pdf - $@

# cleaning

clean:
	rm -f *~ *.bak *.html *.dvi *.ps pure $(OBJECT) parser.output

cleanlogs:
	rm -f $(logs)

realclean: clean
	rm -f $(EXTRA_SOURCE) $(logs) $(dist).tar.gz

# dependencies

depend: $(SOURCE) $(EXTRA_SOURCE)
	makedepend -Y $(SOURCE) $(EXTRA_SOURCE) 2> /dev/null

# Strip symbols from the executable. You might want to run this on Linux
# systems before installation to get a smaller executable. *Never* do this on
# OSX though, as the symbols are needed to properly resolve the runtime
# externals which are linked into the executable.

strip: pure
	strip pure

# installation

install: pure $(lib)
	install -d $(DESTDIR)$(bindir) $(DESTDIR)$(libdir) $(DESTDIR)$(mandir)
	install pure $(DESTDIR)$(bindir)/pure
	install -m 644 $(lib) $(DESTDIR)$(libdir)
	install -m 644 pure.1 $(DESTDIR)$(mandir)/pure.1

uninstall:
	rm -rf $(DESTDIR)$(bindir)/pure $(DESTDIR)$(libdir) $(DESTDIR)$(mandir)/pure.1

# roll a distribution tarball

dist: $(DISTFILES)
	rm -rf $(dist)
	mkdir $(dist) && mkdir $(dist)/examples && mkdir $(dist)/lib && \
mkdir $(dist)/test
	for x in $(DISTFILES); do ln -sf $$PWD/$$x $(dist)/$$x; done
	rm -f $(dist).tar.gz
	tar cfzh $(dist).tar.gz $(dist)
	rm -rf $(dist)

distcheck: dist
	tar xfz $(dist).tar.gz
	cd $(dist) && make && make check && make install DESTDIR=./BUILD
	rm -rf $(dist)

# test logs, make check

level=7

logs: $(logs)

check: pure
	@ echo Running tests.
	@ (export PURELIB=./lib; echo -n "prelude.pure: "; if ./pure -n -v$(level) lib/prelude.pure 2>&1 | diff -q - test/prelude.log > /dev/null; then echo passed; else echo FAILED; fi)
	@ (cd test; export PURELIB=../lib; for x in $(tests); do f="`basename $$x`"; l="`basename $$x .pure`.log"; echo -n "$$x: "; if ../pure -v$(level) < $$f 2>&1 | diff -q - $$l > /dev/null; then echo passed; else echo FAILED; fi; done)

test/prelude.log: lib/prelude.pure
	PURELIB=./lib ./pure -n -v$(level) $< > $@ 2>&1

%.log: %.pure
	PURELIB=./lib ./pure -v$(level) < $< > $@ 2>&1

# DO NOT DELETE

expr.o: expr.hh interpreter.hh matcher.hh symtable.hh printer.hh runtime.h
expr.o: parser.hh stack.hh util.hh location.hh position.hh
interpreter.o: interpreter.hh expr.hh matcher.hh symtable.hh printer.hh
interpreter.o: runtime.h parser.hh stack.hh util.hh location.hh position.hh
interpreter.o: expr.hh matcher.hh symtable.hh printer.hh runtime.h parser.hh
interpreter.o: stack.hh util.hh location.hh position.hh
lexer.o: interpreter.hh expr.hh matcher.hh symtable.hh printer.hh runtime.h
lexer.o: parser.hh stack.hh util.hh location.hh position.hh
matcher.o: matcher.hh expr.hh
matcher.o: expr.hh
parser.o: expr.hh printer.hh matcher.hh runtime.h util.hh interpreter.hh
parser.o: symtable.hh parser.hh stack.hh location.hh position.hh
printer.o: printer.hh expr.hh matcher.hh runtime.h interpreter.hh symtable.hh
printer.o: parser.hh stack.hh util.hh location.hh position.hh
printer.o: expr.hh matcher.hh runtime.h
pure.o: interpreter.hh expr.hh matcher.hh symtable.hh printer.hh runtime.h
pure.o: parser.hh stack.hh util.hh location.hh position.hh
runtime.o: runtime.h expr.hh interpreter.hh matcher.hh symtable.hh printer.hh
runtime.o: parser.hh stack.hh util.hh location.hh position.hh funcall.h
symtable.o: symtable.hh expr.hh printer.hh matcher.hh runtime.h
symtable.o: expr.hh printer.hh matcher.hh runtime.h
util.o: util.hh w3centities.c
lexer.o: interpreter.hh expr.hh matcher.hh symtable.hh printer.hh runtime.h
lexer.o: parser.hh stack.hh util.hh location.hh position.hh
parser.o: parser.hh stack.hh expr.hh printer.hh matcher.hh runtime.h util.hh
parser.o: location.hh position.hh interpreter.hh symtable.hh
parser.o: stack.hh expr.hh printer.hh matcher.hh runtime.h util.hh
parser.o: location.hh position.hh
location.o: position.hh
