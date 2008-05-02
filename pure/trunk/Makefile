
# This Makefile requires GNU make. Really.

# Basic setup. You can change the version number and installation paths here.

# For instance, to install under /usr instead of /usr/local, run 'make
# prefix=/usr && make install prefix=/usr'. Please note that the 'prefix'
# option must be specified *both* at build and at installation time. At
# installation time, you can also specify a DESTDIR path if you want to
# install into a staging directory, e.g.: 'make install DESTDIR=$PWD/BUILD'.

version = 0.1
dist = pure-$(version)

prefix = /usr/local
bindir = $(prefix)/bin
libdir = $(prefix)/lib/pure
mandir = $(prefix)/share/man/man1

DESTDIR=

# Compilation flags. We provide three different build profiles:

# 'default' compiles with extra runtime checks and debugging information.
# 'debug'   adds more debugging output (useful to debug the interpreter).
# 'release' optimizes for execution speed (release version).

# The latter disables all runtime checks and debugging information and gives
# you *much* faster execution times (factor of 2 compared to the default flags
# on my Linux system running gcc 4.1, YMMV). It also takes a *long* time to
# compile runtime.cc, so be patient. ;-)

# To build with a given profile, just say 'make build=<profile>', e.g.: 'make
# build=release'. (This option only has to be specified at build time, not for
# installation or any other targets except 'all'.)

build=default

# No need to edit below this line. Unless you really have to. :) ############

LLVM_FLAGS = `llvm-config --cppflags`
LLVM_LIBS = `llvm-config --ldflags --libs core jit native`

# NOTE: Some of the following flags are gcc-specific, so you'll have to fiddle
# with the options if you're using a different compiler.

ifeq ($(build),default)
CXXFLAGS = -g -Wall $(LLVM_FLAGS)
CFLAGS = -g -Wall
else
ifeq ($(build),debug)
CXXFLAGS = -g -Wall -DDEBUG=2 $(LLVM_FLAGS)
CFLAGS = -g -Wall -DDEBUG=2
else
ifeq ($(build),release)
CXXFLAGS = -O3 -DNDEBUG -Wall $(LLVM_FLAGS)
CFLAGS = -O3 -DNDEBUG -Wall
else
CXXFLAGS = -g -Wall $(LLVM_FLAGS)
CFLAGS = -g -Wall
.PHONY: warn
warn: all
	@echo "WARNING: Invalid build profile '$(build)'."
	@echo "WARNING: Must be one of 'default', 'debug' and 'release'."
	@echo "WARNING: Assuming 'default' profile."
endif
endif
endif

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

DISTFILES = COPYING ChangeLog NEWS README TODO Makefile \
$(SOURCE) $(EXTRA_SOURCE) w3centities.c pure.1 pure.xml pure.vim \
$(examples) $(lib) $(tests) $(distlogs)

.PHONY: all html dvi ps pdf clean realclean depend install uninstall dist \
logs check

# compilation

all: pure

pure: $(OBJECT)
	$(CXX) -o $@ $(LDFLAGS) -rdynamic $(OBJECT) $(ALL_LIBS)

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

# installation

install: pure $(lib)
	install -d $(DESTDIR)$(bindir) $(DESTDIR)$(libdir) $(DESTDIR)$(mandir)
	install -s pure $(DESTDIR)$(bindir)/pure
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

# test logs, make check

level=7

logs: $(logs)

check: pure $(logs)
	@ echo Running tests.
	@ (export PURELIB=./lib; echo -n "prelude.pure: "; if ./pure -n -v$(level) lib/prelude.pure | diff -q - test/prelude.log > /dev/null; then echo passed; else echo FAILED; fi)
	@ (cd test; export PURELIB=../lib; for x in $(tests); do f="`basename $$x`"; l="`basename $$x .pure`.log"; echo -n "$$x: "; if ../pure -v$(level) $$f | diff -q - $$l > /dev/null; then echo passed; else echo FAILED; fi; done)

test/prelude.log: lib/prelude.pure
	PURELIB=./lib ./pure -n -v$(level) $< > $@

%.log: %.pure
	PURELIB=./lib ./pure -v$(level) $< > $@

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
