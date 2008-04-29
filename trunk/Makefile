
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
LIBS = $(LLVM_LIBS) -lreadline -lgmp

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
	g++ -o $@ -rdynamic $(OBJECT) $(LIBS)

pure.o: pure.cc
	g++ $(CXXFLAGS) -DVERSION='"$(version)"' -DPURELIB='"$(libdir)"' -c -o $@ $<

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
	makedepend -Y -- $(CXXFLAGS) -- $(SOURCE) $(EXTRA_SOURCE) 2> /dev/null

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

expr.o: expr.hh interpreter.hh /usr/local/include/llvm/DerivedTypes.h
expr.o: /usr/local/include/llvm/Type.h
expr.o: /usr/local/include/llvm/AbstractTypeUser.h
expr.o: /usr/local/include/llvm/Support/Casting.h
expr.o: /usr/local/include/llvm/Support/DataTypes.h
expr.o: /usr/local/include/llvm/Support/Streams.h
expr.o: /usr/local/include/llvm/ADT/GraphTraits.h
expr.o: /usr/local/include/llvm/ADT/iterator
expr.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
expr.o: /usr/local/include/llvm/System/Mutex.h
expr.o: /usr/local/include/llvm/ADT/SmallVector.h
expr.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
expr.o: /usr/local/include/llvm/GlobalValue.h
expr.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
expr.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
expr.o: /usr/local/include/llvm/BasicBlock.h
expr.o: /usr/local/include/llvm/Instruction.h
expr.o: /usr/local/include/llvm/Instruction.def
expr.o: /usr/local/include/llvm/SymbolTableListTraits.h
expr.o: /usr/local/include/llvm/ADT/ilist /usr/local/include/llvm/Argument.h
expr.o: /usr/local/include/llvm/Support/Annotation.h
expr.o: /usr/local/include/llvm/GlobalVariable.h
expr.o: /usr/local/include/llvm/GlobalAlias.h
expr.o: /usr/local/include/llvm/ModuleProvider.h
expr.o: /usr/local/include/llvm/PassManager.h /usr/local/include/llvm/Pass.h
expr.o: /usr/local/include/llvm/PassSupport.h
expr.o: /usr/local/include/llvm/System/IncludeFile.h
expr.o: /usr/local/include/llvm/PassAnalysisSupport.h
expr.o: /usr/local/include/llvm/Analysis/Verifier.h
expr.o: /usr/local/include/llvm/Target/TargetData.h
expr.o: /usr/local/include/llvm/Transforms/Scalar.h
expr.o: /usr/local/include/llvm/Support/LLVMBuilder.h
expr.o: /usr/local/include/llvm/Instructions.h
expr.o: /usr/local/include/llvm/InstrTypes.h
expr.o: /usr/local/include/llvm/Constants.h
expr.o: /usr/local/include/llvm/ADT/APInt.h
expr.o: /usr/local/include/llvm/ADT/APFloat.h matcher.hh symtable.hh
expr.o: printer.hh runtime.h
interpreter.o: interpreter.hh /usr/local/include/llvm/DerivedTypes.h
interpreter.o: /usr/local/include/llvm/Type.h
interpreter.o: /usr/local/include/llvm/AbstractTypeUser.h
interpreter.o: /usr/local/include/llvm/Support/Casting.h
interpreter.o: /usr/local/include/llvm/Support/DataTypes.h
interpreter.o: /usr/local/include/llvm/Support/Streams.h
interpreter.o: /usr/local/include/llvm/ADT/GraphTraits.h
interpreter.o: /usr/local/include/llvm/ADT/iterator
interpreter.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
interpreter.o: /usr/local/include/llvm/System/Mutex.h
interpreter.o: /usr/local/include/llvm/ADT/SmallVector.h
interpreter.o: /usr/local/include/llvm/Module.h
interpreter.o: /usr/local/include/llvm/Function.h
interpreter.o: /usr/local/include/llvm/GlobalValue.h
interpreter.o: /usr/local/include/llvm/Constant.h
interpreter.o: /usr/local/include/llvm/User.h /usr/local/include/llvm/Value.h
interpreter.o: /usr/local/include/llvm/Use.h
interpreter.o: /usr/local/include/llvm/BasicBlock.h
interpreter.o: /usr/local/include/llvm/Instruction.h
interpreter.o: /usr/local/include/llvm/Instruction.def
interpreter.o: /usr/local/include/llvm/SymbolTableListTraits.h
interpreter.o: /usr/local/include/llvm/ADT/ilist
interpreter.o: /usr/local/include/llvm/Argument.h
interpreter.o: /usr/local/include/llvm/Support/Annotation.h
interpreter.o: /usr/local/include/llvm/GlobalVariable.h
interpreter.o: /usr/local/include/llvm/GlobalAlias.h
interpreter.o: /usr/local/include/llvm/ModuleProvider.h
interpreter.o: /usr/local/include/llvm/PassManager.h
interpreter.o: /usr/local/include/llvm/Pass.h
interpreter.o: /usr/local/include/llvm/PassSupport.h
interpreter.o: /usr/local/include/llvm/System/IncludeFile.h
interpreter.o: /usr/local/include/llvm/PassAnalysisSupport.h
interpreter.o: /usr/local/include/llvm/Analysis/Verifier.h
interpreter.o: /usr/local/include/llvm/Target/TargetData.h
interpreter.o: /usr/local/include/llvm/Transforms/Scalar.h
interpreter.o: /usr/local/include/llvm/Support/LLVMBuilder.h
interpreter.o: /usr/local/include/llvm/Instructions.h
interpreter.o: /usr/local/include/llvm/InstrTypes.h
interpreter.o: /usr/local/include/llvm/Constants.h
interpreter.o: /usr/local/include/llvm/ADT/APInt.h
interpreter.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh
interpreter.o: symtable.hh printer.hh runtime.h parser.hh util.hh stack.hh
interpreter.o: location.hh position.hh /usr/local/include/llvm/CallingConv.h
interpreter.o: /usr/local/include/llvm/System/DynamicLibrary.h
interpreter.o: /usr/local/include/llvm/System/Path.h
interpreter.o: /usr/local/include/llvm/System/TimeValue.h
interpreter.o: /usr/local/include/llvm/Transforms/Utils/BasicBlockUtils.h
interpreter.o: /usr/local/include/llvm/Support/CFG.h
interpreter.o: /usr/local/include/llvm/DerivedTypes.h
interpreter.o: /usr/local/include/llvm/Type.h
interpreter.o: /usr/local/include/llvm/AbstractTypeUser.h
interpreter.o: /usr/local/include/llvm/Support/Casting.h
interpreter.o: /usr/local/include/llvm/Support/DataTypes.h
interpreter.o: /usr/local/include/llvm/Support/Streams.h
interpreter.o: /usr/local/include/llvm/ADT/GraphTraits.h
interpreter.o: /usr/local/include/llvm/ADT/iterator
interpreter.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
interpreter.o: /usr/local/include/llvm/System/Mutex.h
interpreter.o: /usr/local/include/llvm/ADT/SmallVector.h
interpreter.o: /usr/local/include/llvm/Module.h
interpreter.o: /usr/local/include/llvm/Function.h
interpreter.o: /usr/local/include/llvm/GlobalValue.h
interpreter.o: /usr/local/include/llvm/Constant.h
interpreter.o: /usr/local/include/llvm/User.h /usr/local/include/llvm/Value.h
interpreter.o: /usr/local/include/llvm/Use.h
interpreter.o: /usr/local/include/llvm/BasicBlock.h
interpreter.o: /usr/local/include/llvm/Instruction.h
interpreter.o: /usr/local/include/llvm/Instruction.def
interpreter.o: /usr/local/include/llvm/SymbolTableListTraits.h
interpreter.o: /usr/local/include/llvm/ADT/ilist
interpreter.o: /usr/local/include/llvm/Argument.h
interpreter.o: /usr/local/include/llvm/Support/Annotation.h
interpreter.o: /usr/local/include/llvm/GlobalVariable.h
interpreter.o: /usr/local/include/llvm/GlobalAlias.h
interpreter.o: /usr/local/include/llvm/ModuleProvider.h
interpreter.o: /usr/local/include/llvm/PassManager.h
interpreter.o: /usr/local/include/llvm/Pass.h
interpreter.o: /usr/local/include/llvm/PassSupport.h
interpreter.o: /usr/local/include/llvm/System/IncludeFile.h
interpreter.o: /usr/local/include/llvm/PassAnalysisSupport.h
interpreter.o: /usr/local/include/llvm/Analysis/Verifier.h
interpreter.o: /usr/local/include/llvm/Target/TargetData.h
interpreter.o: /usr/local/include/llvm/Transforms/Scalar.h
interpreter.o: /usr/local/include/llvm/Support/LLVMBuilder.h
interpreter.o: /usr/local/include/llvm/Instructions.h
interpreter.o: /usr/local/include/llvm/InstrTypes.h
interpreter.o: /usr/local/include/llvm/Constants.h
interpreter.o: /usr/local/include/llvm/ADT/APInt.h
interpreter.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh
interpreter.o: symtable.hh printer.hh runtime.h
lexer.o: interpreter.hh /usr/local/include/llvm/DerivedTypes.h
lexer.o: /usr/local/include/llvm/Type.h
lexer.o: /usr/local/include/llvm/AbstractTypeUser.h
lexer.o: /usr/local/include/llvm/Support/Casting.h
lexer.o: /usr/local/include/llvm/Support/DataTypes.h
lexer.o: /usr/local/include/llvm/Support/Streams.h
lexer.o: /usr/local/include/llvm/ADT/GraphTraits.h
lexer.o: /usr/local/include/llvm/ADT/iterator
lexer.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
lexer.o: /usr/local/include/llvm/System/Mutex.h
lexer.o: /usr/local/include/llvm/ADT/SmallVector.h
lexer.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
lexer.o: /usr/local/include/llvm/GlobalValue.h
lexer.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
lexer.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
lexer.o: /usr/local/include/llvm/BasicBlock.h
lexer.o: /usr/local/include/llvm/Instruction.h
lexer.o: /usr/local/include/llvm/Instruction.def
lexer.o: /usr/local/include/llvm/SymbolTableListTraits.h
lexer.o: /usr/local/include/llvm/ADT/ilist /usr/local/include/llvm/Argument.h
lexer.o: /usr/local/include/llvm/Support/Annotation.h
lexer.o: /usr/local/include/llvm/GlobalVariable.h
lexer.o: /usr/local/include/llvm/GlobalAlias.h
lexer.o: /usr/local/include/llvm/ModuleProvider.h
lexer.o: /usr/local/include/llvm/PassManager.h /usr/local/include/llvm/Pass.h
lexer.o: /usr/local/include/llvm/PassSupport.h
lexer.o: /usr/local/include/llvm/System/IncludeFile.h
lexer.o: /usr/local/include/llvm/PassAnalysisSupport.h
lexer.o: /usr/local/include/llvm/Analysis/Verifier.h
lexer.o: /usr/local/include/llvm/Target/TargetData.h
lexer.o: /usr/local/include/llvm/Transforms/Scalar.h
lexer.o: /usr/local/include/llvm/Support/LLVMBuilder.h
lexer.o: /usr/local/include/llvm/Instructions.h
lexer.o: /usr/local/include/llvm/InstrTypes.h
lexer.o: /usr/local/include/llvm/Constants.h
lexer.o: /usr/local/include/llvm/ADT/APInt.h
lexer.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh symtable.hh
lexer.o: printer.hh runtime.h parser.hh util.hh stack.hh location.hh
lexer.o: position.hh
matcher.o: matcher.hh expr.hh
matcher.o: expr.hh
parser.o: interpreter.hh /usr/local/include/llvm/DerivedTypes.h
parser.o: /usr/local/include/llvm/Type.h
parser.o: /usr/local/include/llvm/AbstractTypeUser.h
parser.o: /usr/local/include/llvm/Support/Casting.h
parser.o: /usr/local/include/llvm/Support/DataTypes.h
parser.o: /usr/local/include/llvm/Support/Streams.h
parser.o: /usr/local/include/llvm/ADT/GraphTraits.h
parser.o: /usr/local/include/llvm/ADT/iterator
parser.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
parser.o: /usr/local/include/llvm/System/Mutex.h
parser.o: /usr/local/include/llvm/ADT/SmallVector.h
parser.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
parser.o: /usr/local/include/llvm/GlobalValue.h
parser.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
parser.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
parser.o: /usr/local/include/llvm/BasicBlock.h
parser.o: /usr/local/include/llvm/Instruction.h
parser.o: /usr/local/include/llvm/Instruction.def
parser.o: /usr/local/include/llvm/SymbolTableListTraits.h
parser.o: /usr/local/include/llvm/ADT/ilist
parser.o: /usr/local/include/llvm/Argument.h
parser.o: /usr/local/include/llvm/Support/Annotation.h
parser.o: /usr/local/include/llvm/GlobalVariable.h
parser.o: /usr/local/include/llvm/GlobalAlias.h
parser.o: /usr/local/include/llvm/ModuleProvider.h
parser.o: /usr/local/include/llvm/PassManager.h
parser.o: /usr/local/include/llvm/Pass.h
parser.o: /usr/local/include/llvm/PassSupport.h
parser.o: /usr/local/include/llvm/System/IncludeFile.h
parser.o: /usr/local/include/llvm/PassAnalysisSupport.h
parser.o: /usr/local/include/llvm/Analysis/Verifier.h
parser.o: /usr/local/include/llvm/Target/TargetData.h
parser.o: /usr/local/include/llvm/Transforms/Scalar.h
parser.o: /usr/local/include/llvm/Support/LLVMBuilder.h
parser.o: /usr/local/include/llvm/Instructions.h
parser.o: /usr/local/include/llvm/InstrTypes.h
parser.o: /usr/local/include/llvm/Constants.h
parser.o: /usr/local/include/llvm/ADT/APInt.h
parser.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh
parser.o: symtable.hh printer.hh runtime.h util.hh
printer.o: printer.hh expr.hh matcher.hh runtime.h interpreter.hh
printer.o: /usr/local/include/llvm/DerivedTypes.h
printer.o: /usr/local/include/llvm/Type.h
printer.o: /usr/local/include/llvm/AbstractTypeUser.h
printer.o: /usr/local/include/llvm/Support/Casting.h
printer.o: /usr/local/include/llvm/Support/DataTypes.h
printer.o: /usr/local/include/llvm/Support/Streams.h
printer.o: /usr/local/include/llvm/ADT/GraphTraits.h
printer.o: /usr/local/include/llvm/ADT/iterator
printer.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
printer.o: /usr/local/include/llvm/System/Mutex.h
printer.o: /usr/local/include/llvm/ADT/SmallVector.h
printer.o: /usr/local/include/llvm/Module.h
printer.o: /usr/local/include/llvm/Function.h
printer.o: /usr/local/include/llvm/GlobalValue.h
printer.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
printer.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
printer.o: /usr/local/include/llvm/BasicBlock.h
printer.o: /usr/local/include/llvm/Instruction.h
printer.o: /usr/local/include/llvm/Instruction.def
printer.o: /usr/local/include/llvm/SymbolTableListTraits.h
printer.o: /usr/local/include/llvm/ADT/ilist
printer.o: /usr/local/include/llvm/Argument.h
printer.o: /usr/local/include/llvm/Support/Annotation.h
printer.o: /usr/local/include/llvm/GlobalVariable.h
printer.o: /usr/local/include/llvm/GlobalAlias.h
printer.o: /usr/local/include/llvm/ModuleProvider.h
printer.o: /usr/local/include/llvm/PassManager.h
printer.o: /usr/local/include/llvm/Pass.h
printer.o: /usr/local/include/llvm/PassSupport.h
printer.o: /usr/local/include/llvm/System/IncludeFile.h
printer.o: /usr/local/include/llvm/PassAnalysisSupport.h
printer.o: /usr/local/include/llvm/Analysis/Verifier.h
printer.o: /usr/local/include/llvm/Target/TargetData.h
printer.o: /usr/local/include/llvm/Transforms/Scalar.h
printer.o: /usr/local/include/llvm/Support/LLVMBuilder.h
printer.o: /usr/local/include/llvm/Instructions.h
printer.o: /usr/local/include/llvm/InstrTypes.h
printer.o: /usr/local/include/llvm/Constants.h
printer.o: /usr/local/include/llvm/ADT/APInt.h
printer.o: /usr/local/include/llvm/ADT/APFloat.h symtable.hh util.hh
printer.o: expr.hh matcher.hh runtime.h
pure.o: interpreter.hh /usr/local/include/llvm/DerivedTypes.h
pure.o: /usr/local/include/llvm/Type.h
pure.o: /usr/local/include/llvm/AbstractTypeUser.h
pure.o: /usr/local/include/llvm/Support/Casting.h
pure.o: /usr/local/include/llvm/Support/DataTypes.h
pure.o: /usr/local/include/llvm/Support/Streams.h
pure.o: /usr/local/include/llvm/ADT/GraphTraits.h
pure.o: /usr/local/include/llvm/ADT/iterator
pure.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
pure.o: /usr/local/include/llvm/System/Mutex.h
pure.o: /usr/local/include/llvm/ADT/SmallVector.h
pure.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
pure.o: /usr/local/include/llvm/GlobalValue.h
pure.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
pure.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
pure.o: /usr/local/include/llvm/BasicBlock.h
pure.o: /usr/local/include/llvm/Instruction.h
pure.o: /usr/local/include/llvm/Instruction.def
pure.o: /usr/local/include/llvm/SymbolTableListTraits.h
pure.o: /usr/local/include/llvm/ADT/ilist /usr/local/include/llvm/Argument.h
pure.o: /usr/local/include/llvm/Support/Annotation.h
pure.o: /usr/local/include/llvm/GlobalVariable.h
pure.o: /usr/local/include/llvm/GlobalAlias.h
pure.o: /usr/local/include/llvm/ModuleProvider.h
pure.o: /usr/local/include/llvm/PassManager.h /usr/local/include/llvm/Pass.h
pure.o: /usr/local/include/llvm/PassSupport.h
pure.o: /usr/local/include/llvm/System/IncludeFile.h
pure.o: /usr/local/include/llvm/PassAnalysisSupport.h
pure.o: /usr/local/include/llvm/Analysis/Verifier.h
pure.o: /usr/local/include/llvm/Target/TargetData.h
pure.o: /usr/local/include/llvm/Transforms/Scalar.h
pure.o: /usr/local/include/llvm/Support/LLVMBuilder.h
pure.o: /usr/local/include/llvm/Instructions.h
pure.o: /usr/local/include/llvm/InstrTypes.h
pure.o: /usr/local/include/llvm/Constants.h
pure.o: /usr/local/include/llvm/ADT/APInt.h
pure.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh symtable.hh
pure.o: printer.hh runtime.h /usr/local/include/llvm/Target/TargetOptions.h
runtime.o: runtime.h expr.hh interpreter.hh
runtime.o: /usr/local/include/llvm/DerivedTypes.h
runtime.o: /usr/local/include/llvm/Type.h
runtime.o: /usr/local/include/llvm/AbstractTypeUser.h
runtime.o: /usr/local/include/llvm/Support/Casting.h
runtime.o: /usr/local/include/llvm/Support/DataTypes.h
runtime.o: /usr/local/include/llvm/Support/Streams.h
runtime.o: /usr/local/include/llvm/ADT/GraphTraits.h
runtime.o: /usr/local/include/llvm/ADT/iterator
runtime.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
runtime.o: /usr/local/include/llvm/System/Mutex.h
runtime.o: /usr/local/include/llvm/ADT/SmallVector.h
runtime.o: /usr/local/include/llvm/Module.h
runtime.o: /usr/local/include/llvm/Function.h
runtime.o: /usr/local/include/llvm/GlobalValue.h
runtime.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
runtime.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
runtime.o: /usr/local/include/llvm/BasicBlock.h
runtime.o: /usr/local/include/llvm/Instruction.h
runtime.o: /usr/local/include/llvm/Instruction.def
runtime.o: /usr/local/include/llvm/SymbolTableListTraits.h
runtime.o: /usr/local/include/llvm/ADT/ilist
runtime.o: /usr/local/include/llvm/Argument.h
runtime.o: /usr/local/include/llvm/Support/Annotation.h
runtime.o: /usr/local/include/llvm/GlobalVariable.h
runtime.o: /usr/local/include/llvm/GlobalAlias.h
runtime.o: /usr/local/include/llvm/ModuleProvider.h
runtime.o: /usr/local/include/llvm/PassManager.h
runtime.o: /usr/local/include/llvm/Pass.h
runtime.o: /usr/local/include/llvm/PassSupport.h
runtime.o: /usr/local/include/llvm/System/IncludeFile.h
runtime.o: /usr/local/include/llvm/PassAnalysisSupport.h
runtime.o: /usr/local/include/llvm/Analysis/Verifier.h
runtime.o: /usr/local/include/llvm/Target/TargetData.h
runtime.o: /usr/local/include/llvm/Transforms/Scalar.h
runtime.o: /usr/local/include/llvm/Support/LLVMBuilder.h
runtime.o: /usr/local/include/llvm/Instructions.h
runtime.o: /usr/local/include/llvm/InstrTypes.h
runtime.o: /usr/local/include/llvm/Constants.h
runtime.o: /usr/local/include/llvm/ADT/APInt.h
runtime.o: /usr/local/include/llvm/ADT/APFloat.h matcher.hh symtable.hh
runtime.o: printer.hh util.hh funcall.h
symtable.o: symtable.hh expr.hh printer.hh matcher.hh runtime.h
symtable.o: expr.hh printer.hh matcher.hh runtime.h
util.o: util.hh w3centities.c
lexer.o: interpreter.hh /usr/local/include/llvm/DerivedTypes.h
lexer.o: /usr/local/include/llvm/Type.h
lexer.o: /usr/local/include/llvm/AbstractTypeUser.h
lexer.o: /usr/local/include/llvm/Support/Casting.h
lexer.o: /usr/local/include/llvm/Support/DataTypes.h
lexer.o: /usr/local/include/llvm/Support/Streams.h
lexer.o: /usr/local/include/llvm/ADT/GraphTraits.h
lexer.o: /usr/local/include/llvm/ADT/iterator
lexer.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
lexer.o: /usr/local/include/llvm/System/Mutex.h
lexer.o: /usr/local/include/llvm/ADT/SmallVector.h
lexer.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
lexer.o: /usr/local/include/llvm/GlobalValue.h
lexer.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
lexer.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
lexer.o: /usr/local/include/llvm/BasicBlock.h
lexer.o: /usr/local/include/llvm/Instruction.h
lexer.o: /usr/local/include/llvm/Instruction.def
lexer.o: /usr/local/include/llvm/SymbolTableListTraits.h
lexer.o: /usr/local/include/llvm/ADT/ilist /usr/local/include/llvm/Argument.h
lexer.o: /usr/local/include/llvm/Support/Annotation.h
lexer.o: /usr/local/include/llvm/GlobalVariable.h
lexer.o: /usr/local/include/llvm/GlobalAlias.h
lexer.o: /usr/local/include/llvm/ModuleProvider.h
lexer.o: /usr/local/include/llvm/PassManager.h /usr/local/include/llvm/Pass.h
lexer.o: /usr/local/include/llvm/PassSupport.h
lexer.o: /usr/local/include/llvm/System/IncludeFile.h
lexer.o: /usr/local/include/llvm/PassAnalysisSupport.h
lexer.o: /usr/local/include/llvm/Analysis/Verifier.h
lexer.o: /usr/local/include/llvm/Target/TargetData.h
lexer.o: /usr/local/include/llvm/Transforms/Scalar.h
lexer.o: /usr/local/include/llvm/Support/LLVMBuilder.h
lexer.o: /usr/local/include/llvm/Instructions.h
lexer.o: /usr/local/include/llvm/InstrTypes.h
lexer.o: /usr/local/include/llvm/Constants.h
lexer.o: /usr/local/include/llvm/ADT/APInt.h
lexer.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh symtable.hh
lexer.o: printer.hh runtime.h parser.hh util.hh stack.hh location.hh
lexer.o: position.hh
parser.o: parser.hh interpreter.hh /usr/local/include/llvm/DerivedTypes.h
parser.o: /usr/local/include/llvm/Type.h
parser.o: /usr/local/include/llvm/AbstractTypeUser.h
parser.o: /usr/local/include/llvm/Support/Casting.h
parser.o: /usr/local/include/llvm/Support/DataTypes.h
parser.o: /usr/local/include/llvm/Support/Streams.h
parser.o: /usr/local/include/llvm/ADT/GraphTraits.h
parser.o: /usr/local/include/llvm/ADT/iterator
parser.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
parser.o: /usr/local/include/llvm/System/Mutex.h
parser.o: /usr/local/include/llvm/ADT/SmallVector.h
parser.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
parser.o: /usr/local/include/llvm/GlobalValue.h
parser.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
parser.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
parser.o: /usr/local/include/llvm/BasicBlock.h
parser.o: /usr/local/include/llvm/Instruction.h
parser.o: /usr/local/include/llvm/Instruction.def
parser.o: /usr/local/include/llvm/SymbolTableListTraits.h
parser.o: /usr/local/include/llvm/ADT/ilist
parser.o: /usr/local/include/llvm/Argument.h
parser.o: /usr/local/include/llvm/Support/Annotation.h
parser.o: /usr/local/include/llvm/GlobalVariable.h
parser.o: /usr/local/include/llvm/GlobalAlias.h
parser.o: /usr/local/include/llvm/ModuleProvider.h
parser.o: /usr/local/include/llvm/PassManager.h
parser.o: /usr/local/include/llvm/Pass.h
parser.o: /usr/local/include/llvm/PassSupport.h
parser.o: /usr/local/include/llvm/System/IncludeFile.h
parser.o: /usr/local/include/llvm/PassAnalysisSupport.h
parser.o: /usr/local/include/llvm/Analysis/Verifier.h
parser.o: /usr/local/include/llvm/Target/TargetData.h
parser.o: /usr/local/include/llvm/Transforms/Scalar.h
parser.o: /usr/local/include/llvm/Support/LLVMBuilder.h
parser.o: /usr/local/include/llvm/Instructions.h
parser.o: /usr/local/include/llvm/InstrTypes.h
parser.o: /usr/local/include/llvm/Constants.h
parser.o: /usr/local/include/llvm/ADT/APInt.h
parser.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh
parser.o: symtable.hh printer.hh runtime.h util.hh stack.hh location.hh
parser.o: position.hh
parser.o: interpreter.hh /usr/local/include/llvm/DerivedTypes.h
parser.o: /usr/local/include/llvm/Type.h
parser.o: /usr/local/include/llvm/AbstractTypeUser.h
parser.o: /usr/local/include/llvm/Support/Casting.h
parser.o: /usr/local/include/llvm/Support/DataTypes.h
parser.o: /usr/local/include/llvm/Support/Streams.h
parser.o: /usr/local/include/llvm/ADT/GraphTraits.h
parser.o: /usr/local/include/llvm/ADT/iterator
parser.o: /usr/local/include/llvm/ExecutionEngine/ExecutionEngine.h
parser.o: /usr/local/include/llvm/System/Mutex.h
parser.o: /usr/local/include/llvm/ADT/SmallVector.h
parser.o: /usr/local/include/llvm/Module.h /usr/local/include/llvm/Function.h
parser.o: /usr/local/include/llvm/GlobalValue.h
parser.o: /usr/local/include/llvm/Constant.h /usr/local/include/llvm/User.h
parser.o: /usr/local/include/llvm/Value.h /usr/local/include/llvm/Use.h
parser.o: /usr/local/include/llvm/BasicBlock.h
parser.o: /usr/local/include/llvm/Instruction.h
parser.o: /usr/local/include/llvm/Instruction.def
parser.o: /usr/local/include/llvm/SymbolTableListTraits.h
parser.o: /usr/local/include/llvm/ADT/ilist
parser.o: /usr/local/include/llvm/Argument.h
parser.o: /usr/local/include/llvm/Support/Annotation.h
parser.o: /usr/local/include/llvm/GlobalVariable.h
parser.o: /usr/local/include/llvm/GlobalAlias.h
parser.o: /usr/local/include/llvm/ModuleProvider.h
parser.o: /usr/local/include/llvm/PassManager.h
parser.o: /usr/local/include/llvm/Pass.h
parser.o: /usr/local/include/llvm/PassSupport.h
parser.o: /usr/local/include/llvm/System/IncludeFile.h
parser.o: /usr/local/include/llvm/PassAnalysisSupport.h
parser.o: /usr/local/include/llvm/Analysis/Verifier.h
parser.o: /usr/local/include/llvm/Target/TargetData.h
parser.o: /usr/local/include/llvm/Transforms/Scalar.h
parser.o: /usr/local/include/llvm/Support/LLVMBuilder.h
parser.o: /usr/local/include/llvm/Instructions.h
parser.o: /usr/local/include/llvm/InstrTypes.h
parser.o: /usr/local/include/llvm/Constants.h
parser.o: /usr/local/include/llvm/ADT/APInt.h
parser.o: /usr/local/include/llvm/ADT/APFloat.h expr.hh matcher.hh
parser.o: symtable.hh printer.hh runtime.h util.hh stack.hh location.hh
parser.o: position.hh
location.o: position.hh
