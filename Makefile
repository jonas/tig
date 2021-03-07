## Makefile for tig

# The last tagged version. Can be overridden either by the version from
# git or from the value of the DIST_VERSION environment variable.
VERSION	= 2.5.3

all:

# Include kernel specific configuration
kernel_name := $(shell sh -c 'uname -s 2>/dev/null || echo unknown')
-include contrib/config.make-$(kernel_name)

# Include setting from the configure script
-include config.make

# Optional defaults.
# TIG_ variables are set by contrib/config.make-$(kernel_name).
TIG_NCURSES ?= -lcurses
LDFLAGS ?= $(TIG_LDFLAGS)
CPPFLAGS ?= $(TIG_CPPFLAGS)
LDLIBS ?= $(TIG_NCURSES) $(TIG_LDLIBS)
CFLAGS ?= -Wall -O2 $(TIG_CFLAGS)

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
datarootdir ?= $(prefix)/share
sysconfdir ?= $(prefix)/etc
docdir ?= $(datarootdir)/doc
mandir ?= $(datarootdir)/man
# DESTDIR=

ifneq (,$(wildcard .git))
GITDESC	= $(subst tig-,,$(shell git describe 2>/dev/null))
COMMIT := $(if $(GITDESC),$(GITDESC),$(VERSION)-g$(shell git describe --always))
WTDIRTY	= $(if $(shell git diff-index HEAD 2>/dev/null),-dirty)
VERSION	= $(COMMIT)$(WTDIRTY)
endif
ifdef DIST_VERSION
VERSION = $(DIST_VERSION)
endif

# Split the version "TAG-OFFSET-gSHA1-DIRTY" into "TAG OFFSET"
# and append 0 as a fallback offset for "exact" tagged versions.
RPM_VERLIST = $(filter-out g% dirty,$(subst -, ,$(VERSION))) 0
RPM_VERSION = $(word 1,$(RPM_VERLIST))
RPM_RELEASE = $(word 2,$(RPM_VERLIST))$(if $(WTDIRTY),.dirty)

DFLAGS	= -g -DDEBUG -Werror -O0
EXE	= src/tig
TOOLS	= test/tools/test-graph tools/doc-gen
TXTDOC	= doc/tig.1.adoc doc/tigrc.5.adoc doc/manual.adoc NEWS.adoc README.adoc INSTALL.adoc test/API.adoc
MANDOC	= doc/tig.1 doc/tigrc.5 doc/tigmanual.7
HTMLDOC = doc/tig.1.html doc/tigrc.5.html doc/manual.html README.html INSTALL.html NEWS.html
ALLDOC	= $(MANDOC) $(HTMLDOC) doc/manual.html-chunked doc/manual.pdf

# Never include the release number in the tarname for tagged
# versions.
ifneq ($(if $(DIST_VERSION),$(words $(RPM_VERLIST))),2)
TARNAME	= tig-$(RPM_VERSION)-$(RPM_RELEASE)
else
TARNAME	= tig-$(RPM_VERSION)
endif

override CPPFLAGS += '-DTIG_VERSION="$(VERSION)"'
override CPPFLAGS += '-DSYSCONFDIR="$(sysconfdir)"'
ifdef TIG_USER_CONFIG
override CPPFLAGS += '-DTIG_USER_CONFIG="$(TIG_USER_CONFIG)"'
endif

ASCIIDOC ?= asciidoc
ASCIIDOC_FLAGS = -aversion=$(VERSION) -asysconfdir=$(sysconfdir) -f doc/asciidoc.conf
XMLTO ?= xmlto
DOCBOOK2PDF ?= docbook2pdf

LCOV ?= lcov
GENHTML ?= genhtml
ifneq (,$(shell which gsed 2>/dev/null))
SED ?= gsed
else
SED ?= sed
endif
ifneq (,$(shell which gtar 2>/dev/null))
TAR ?= gtar
else
TAR ?= tar
endif

GENERATE_COMPILATION_DATABASE ?= no
ifeq ($(GENERATE_COMPILATION_DATABASE),yes)
compdb_check = $(shell $(CC) $(ALL_CFLAGS) \
	-c -MJ /dev/null \
	-x c /dev/null -o /dev/null 2>&1; \
	echo $$?)
ifneq ($(compdb_check),0)
override GENERATE_COMPILATION_DATABASE = no
$(warning GENERATE_COMPILATION_DATABASE is set to "yes", but your compiler does not \
support generating compilation database entries)
endif
else
ifneq ($(GENERATE_COMPILATION_DATABASE),no)
$(error please set GENERATE_COMPILATION_DATABASE to "yes" or "no" \
(not "$(GENERATE_COMPILATION_DATABASE)"))
endif
endif

compdb_dir = compile_commands
ifeq ($(GENERATE_COMPILATION_DATABASE),yes)
missing_compdb_dir = $(compdb_dir)
$(missing_compdb_dir):
	@mkdir -p $@

compdb_file = $(compdb_dir)/$(subst /,-,$@.json)
compdb_args = -MJ $(compdb_file)
else
missing_compdb_dir = 
compdb_args =
endif

all: $(EXE) $(TOOLS)
all-debug: all
all-debug: CFLAGS += $(DFLAGS)
doc: $(ALLDOC)
doc-man: $(MANDOC)
doc-html: $(HTMLDOC)

ifeq ($(GENERATE_COMPILATION_DATABASE),yes)
all: compile_commands.json
compile_commands.json:
	@rm -f $@
	$(QUIET_GEN)sed -e '1s/^/[/' -e '$$s/,$$/]/' $(compdb_dir)/*.o.json > $@+
	@if test -s $@+; then mv $@+ $@; else $(RM) $@+; fi
endif

export sysconfdir

install: all
	$(QUIET_INSTALL)tools/install.sh bin $(EXE) "$(DESTDIR)$(bindir)"
	$(QUIET_INSTALL)tools/install.sh data tigrc "$(DESTDIR)$(sysconfdir)"

install-doc-man: doc-man
	$(Q)$(foreach doc, $(filter %.1, $(MANDOC)), \
		$(QUIET_INSTALL_EACH)tools/install.sh data $(doc) "$(DESTDIR)$(mandir)/man1";)
	$(Q)$(foreach doc, $(filter %.5, $(MANDOC)), \
		$(QUIET_INSTALL_EACH)tools/install.sh data $(doc) "$(DESTDIR)$(mandir)/man5";)
	$(Q)$(foreach doc, $(filter %.7, $(MANDOC)), \
		$(QUIET_INSTALL_EACH)tools/install.sh data $(doc) "$(DESTDIR)$(mandir)/man7";)

install-release-doc-man:
	GIT_INDEX_FILE=.tmp-doc-index git read-tree origin/release
	GIT_INDEX_FILE=.tmp-doc-index git checkout-index -f --prefix=./ $(MANDOC)
	rm -f .tmp-doc-index
	$(MAKE) install-doc-man

install-doc-html: doc-html
	$(Q)$(foreach doc, $(HTMLDOC), \
		$(QUIET_INSTALL_EACH)tools/install.sh data $(doc) "$(DESTDIR)$(docdir)/tig";)

install-release-doc-html:
	GIT_INDEX_FILE=.tmp-doc-index git read-tree origin/release
	GIT_INDEX_FILE=.tmp-doc-index git checkout-index -f --prefix=./ $(HTMLDOC)
	rm -f .tmp-doc-index
	$(MAKE) install-doc-html

install-doc: install-doc-man install-doc-html
install-release-doc: install-release-doc-man install-release-doc-html

uninstall:
	$(QUIET_UNINSTALL)tools/uninstall.sh "$(DESTDIR)$(bindir)/$(EXE:src/%=%)"
	$(QUIET_UNINSTALL)tools/uninstall.sh "$(DESTDIR)$(sysconfdir)/tigrc"
	$(Q)$(foreach doc, $(filter %.1, $(MANDOC:doc/%=%)), \
		$(QUIET_UNINSTALL_EACH)tools/uninstall.sh "$(DESTDIR)$(mandir)/man1/$(doc)";)
	$(Q)$(foreach doc, $(filter %.5, $(MANDOC:doc/%=%)), \
		$(QUIET_UNINSTALL_EACH)tools/uninstall.sh "$(DESTDIR)$(mandir)/man5/$(doc)";)
	$(Q)$(foreach doc, $(filter %.7, $(MANDOC:doc/%=%)), \
		$(QUIET_UNINSTALL_EACH)tools/uninstall.sh "$(DESTDIR)$(mandir)/man7/$(doc)";)
	$(Q)$(foreach doc, $(HTMLDOC:doc/%=%), \
		$(QUIET_UNINSTALL_EACH)tools/uninstall.sh "$(DESTDIR)$(docdir)/tig/$(doc)";)

clean: clean-test clean-coverage
	$(Q)$(RM) -r $(TARNAME) *.spec tig-*.tar.gz tig-*.tar.gz.sha256 .deps _book node_modules
	$(Q)$(RM) -r $(compdb_dir) compile_commands.json
	$(Q)$(RM) $(EXE) $(TOOLS) $(OBJS) core doc/*.xml src/builtin-config.c
	$(Q)$(RM) $(OBJS:%.o=%.gcda) $(OBJS:%.o=%.gcno)

distclean: clean
	$(RM) -r doc/manual.html-chunked autom4te.cache
	$(RM) doc/*.toc $(ALLDOC) aclocal.m4 configure
	$(RM) config.h config.log config.make config.status config.h.in

spell-check:
	for file in $(TXTDOC) src/tig.c; do \
		aspell --lang=en --dont-backup \
		       --personal=./tools/aspell.dict check $$file; \
	done

strip: $(EXE)
	strip $(EXE)

update-headers:
	@for file in include/tig/*.h src/*.c tools/*.c test/tools/*.c; do \
		grep -q '/* Copyright' "$$file" && \
			$(SED) '0,/.*\*\//d' < "$$file" | \
			grep -v '/* vim: set' > "$$file.tmp"; \
		{ cat tools/header.h "$$file.tmp"; \
		  echo "/* vim: set ts=8 sw=8 noexpandtab: */"; } > "$$file"; \
		rm "$$file.tmp"; \
		echo "Updated $$file"; \
	done

update-docs: tools/doc-gen
	doc="doc/tigrc.5.adoc"; \
	$(SED) -n '0,/ifndef::DOC_GEN_ACTIONS/p' < "$$doc" > "$$doc.gen"; \
	./tools/doc-gen actions >> "$$doc.gen"; \
	$(SED) -n '/endif::DOC_GEN_ACTIONS/,$$p' < "$$doc" >> "$$doc.gen" ; \
	mv "$$doc.gen" "$$doc"

dist: configure tig.spec
	$(Q)mkdir -p $(TARNAME) && \
	cp Makefile tig.spec configure config.h.in aclocal.m4 $(TARNAME) && \
	$(SED) -i "s/VERSION\s\+=\s\+[0-9]\+\([.][0-9]\+\)\+/VERSION	= $(VERSION)/" $(TARNAME)/Makefile
	git archive --format=tar --prefix=$(TARNAME)/ HEAD | \
	$(TAR) --delete $(TARNAME)/Makefile > $(TARNAME).tar && \
	find $(TARNAME) -type f -print0 | LC_ALL=C sort -z | $(TAR) --mtime=$(shell git show -s --format=@%ct) --mode=o=rX,ug+rw,a-s --owner=root --group=root --null -T - -rf $(TARNAME).tar && \
	gzip -f -n -9 $(TARNAME).tar && \
	sha256sum $(TARNAME).tar.gz > $(TARNAME).tar.gz.sha256
	$(Q)$(RM) -r $(TARNAME)

rpm: dist
	rpmbuild -ta $(TARNAME).tar.gz

COVERAGE_CFLAGS ?= -fprofile-arcs -ftest-coverage
all-coverage: all
all-coverage: CFLAGS += $(COVERAGE_CFLAGS)

COVERAGE_DIR 	?= test/coverage

clean-coverage:
	@$(RM) -rf $(COVERAGE_DIR)

reset-coverage: clean-coverage
	$(LCOV) --reset-counters

test-coverage: clean-coverage all-coverage test $(COVERAGE_DIR)/index.html

$(COVERAGE_DIR)/trace:
	@mkdir -p $(COVERAGE_DIR)
	$(QUIET_LCOV)$(LCOV) $(Q:@=--quiet) --capture --no-external --directory . --output-file $@

$(COVERAGE_DIR)/index.html: $(COVERAGE_DIR)/trace
	$(QUIET_GENHTML)$(GENHTML) $(Q:@=--quiet) --output-directory $(COVERAGE_DIR) $<

ADDRESS_SANITIZER_CFLAGS ?= -fsanitize=address -fno-omit-frame-pointer
all-address-sanitizer: all
all-address-sanitizer: CFLAGS += $(ADDRESS_SANITIZER_CFLAGS)

test-address-sanitizer: clean all-address-sanitizer test
test-address-sanitizer: export TIG_ADDRESS_SANITIZER_ENABLED=yes

TESTS  = $(sort $(shell find test -type f -name '*-test'))
TESTS_TODO = $(sort $(shell find test -type f -name '*-test' -exec grep -l '\(test_todo\|-todo=\)' {} \+))

clean-test:
	$(Q)$(RM) -r test/tmp

test: clean-test $(TESTS)
	$(QUIET_SUMMARY)test/tools/show-results.sh

ifneq (,$(strip $(V:@=)))
export MAKE_TEST_OPTS = no-indent
else
export MAKE_TEST_OPTS =
endif

$(TESTS): PATH := $(CURDIR)/test/tools:$(CURDIR)/src:$(PATH)
$(TESTS): $(EXE) test/tools/test-graph
	$(QUIET_TEST)$(TEST_SHELL) $@

test-todo: MAKE_TEST_OPTS += todo
test-todo: $(TESTS_TODO)

# Other autoconf-related rules are hidden in config.make.in so that
# they don't confuse Make when we aren't actually using ./configure
configure: configure.ac tools/*.m4
	$(QUIET_GEN)./autogen.sh

site:
	gitbook install
	gitbook build
	find _book -type f | grep -E -v '(gitbook|json|html)' | xargs rm

.PHONY: all all-coverage all-debug clean clean-coverage clean-test doc \
	doc-man doc-html dist distclean install install-doc \
	install-doc-man install-doc-html install-release-doc-html \
	install-release-doc-man rpm spell-check strip test \
	test-coverage update-docs update-headers $(TESTS)

ifdef NO_MKSTEMPS
COMPAT_CPPFLAGS += -DNO_MKSTEMPS
COMPAT_OBJS += compat/mkstemps.o
endif

ifdef NO_SETENV
COMPAT_CPPFLAGS += -DNO_SETENV
COMPAT_OBJS += compat/setenv.o
endif

ifdef NO_STRNDUP
COMPAT_CPPFLAGS += -DNO_STRNDUP
COMPAT_OBJS += compat/strndup.o
endif

ifdef NO_WORDEXP
COMPAT_CPPFLAGS += -DNO_WORDEXP
COMPAT_OBJS += compat/wordexp.o
endif

COMPAT_OBJS += compat/hashtab.o compat/utf8proc.o

override CPPFLAGS += $(COMPAT_CPPFLAGS)

GRAPH_OBJS = src/graph.o src/graph-v1.o src/graph-v2.o

TIG_OBJS = \
	src/tig.o \
	src/types.o \
	src/string.o \
	src/util.o \
	src/map.o \
	src/argv.o \
	src/io.o \
	src/refdb.o \
	src/builtin-config.o \
	src/request.o \
	src/line.o \
	src/keys.o \
	src/repo.o \
	src/options.o \
	src/draw.o \
	src/prompt.o \
	src/display.o \
	src/view.o \
	src/search.o \
	src/parse.o \
	src/watch.o \
	src/pager.o \
	src/log.o \
	src/reflog.o \
	src/diff.o \
	src/help.o \
	src/tree.o \
	src/blob.o \
	src/blame.o \
	src/refs.o \
	src/status.o \
	src/stage.o \
	src/main.o \
	src/stash.o \
	src/grep.o \
	src/ui.o \
	src/apps.o \
	$(GRAPH_OBJS) \
	$(COMPAT_OBJS)

src/tig: $(TIG_OBJS)

TEST_GRAPH_OBJS = test/tools/test-graph.o src/string.o src/util.o src/io.o $(GRAPH_OBJS) $(COMPAT_OBJS)
test/tools/test-graph: $(TEST_GRAPH_OBJS)

DOC_GEN_OBJS = tools/doc-gen.o src/string.o src/types.o src/util.o src/request.o $(COMPAT_OBJS)
tools/doc-gen: $(DOC_GEN_OBJS)

OBJS = $(sort $(TIG_OBJS) $(TEST_GRAPH_OBJS) $(DOC_GEN_OBJS))

DEPS_CFLAGS ?= -MMD -MP -MF .deps/$*.d

%: %.o
	$(QUIET_LINK)$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c $(CONFIG_H) $(missing_compdb_dir)
	@mkdir -p .deps/$(*D)
	$(QUIET_CC)$(CC) -I. -Iinclude $(compdb_args) $(CFLAGS) $(DEPS_CFLAGS) $(CPPFLAGS) -c -o $@ $<

-include $(OBJS:%.o=.deps/%.d)

src/builtin-config.c: tigrc tools/make-builtin-config.sh
	$(QUIET_GEN)tools/make-builtin-config.sh $< > $@

tig.spec: contrib/tig.spec.in
	$(QUIET_GEN)$(SED) -e 's/@@VERSION@@/$(RPM_VERSION)/g' \
	    -e 's/@@RELEASE@@/$(RPM_RELEASE)/g' < $< > $@

doc/manual.html: doc/manual.toc
doc/manual.html: ASCIIDOC_FLAGS += -ainclude-manual-toc
%.toc: %.adoc
	$(QUIET_GEN)$(SED) -n '/^\[\[/,/\(---\|~~~\)/p' < $< | while read line; do \
		case "$$line" in \
		"----"*)  echo ". <<$$ref>>"; ref= ;; \
		"~~~~"*)  echo "- <<$$ref>>"; ref= ;; \
		"[["*"]]") ref="$$line" ;; \
		*)	   ref="$$ref, $$line" ;; \
		esac; done | $(SED) 's/\[\[\(.*\)\]\]/\1/' > $@

README.html: README.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article -a readme $<

INSTALL.html: INSTALL.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article $<

NEWS.html: NEWS.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article $<

doc/tigmanual.7: doc/manual.adoc

test/API.adoc: test/tools/libtest.sh
	@printf '%s\n%s\n' 'Testing API' '-----------' > $@
	$(QUIET_ASCIIDOC)egrep '^#\|' test/tools/libtest.sh | $(SED) 's/^#| \{0,1\}//' | cat -s >> $@

%.1.html : %.1.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d manpage $<

%.1.xml : %.1.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.5.html : %.5.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d manpage $<

%.5.xml : %.5.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.7.xml : %.7.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.html: ASCIIDOC_FLAGS += -adocext=html
%.html : %.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article -n $<

%.xml : %.adoc doc/asciidoc.conf
	$(QUIET_ASCIIDOC)$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d article $<

% : %.xml
	$(QUIET_XMLTO)$(XMLTO) man -o doc $<

%.html-chunked : %.xml
	$(QUIET_XMLTO)$(XMLTO) html -o $@ $<

%.pdf : %.xml
	$(QUIET_DB2PDF)$(DOCBOOK2PDF) -o doc $<

#############################################################################
# Quiet make
#############################################################################

ifneq ($(findstring $(MAKEFLAGS),s),s)
V			= @
Q			= $(V:1=)
QUIET_CC		= $(Q:@=@echo    '        CC  '$@;)
QUIET_LINK		= $(Q:@=@echo    '      LINK  '$@;)
QUIET_GEN		= $(Q:@=@echo    '       GEN  '$@;)
QUIET_ASCIIDOC		= $(Q:@=@echo    '  ASCIIDOC  '$@;)
QUIET_XMLTO		= $(Q:@=@echo    '     XMLTO  '$@;)
QUIET_DB2PDF		= $(Q:@=@echo    '    DB2PDF  '$@;)
# tools/install.sh will print 'file -> $install_dir/file'
QUIET_INSTALL		= $(Q:@=@printf  '   INSTALL  ';)
QUIET_INSTALL_EACH	= $(Q:@=printf   '   INSTALL  ';)
QUIET_UNINSTALL		= $(Q:@=@printf  ' UNINSTALL  ';)
QUIET_UNINSTALL_EACH	= $(Q:@=printf   ' UNINSTALL  ';)
QUIET_TEST		= $(Q:@=@echo    '      TEST  '$@;)
QUIET_SUMMARY		= $(Q:@=@printf  '   SUMMARY  ';)
QUIET_LCOV		= $(Q:@=@echo    '      LCOV  '$@;)
QUIET_GENHTML		= $(Q:@=@echo    '   GENHTML  '$@;)

export V
endif
