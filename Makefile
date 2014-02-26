## Makefile for tig

# The last tagged version. Can be overridden either by the version from
# git or from the value of the DIST_VERSION environment variable.
VERSION	= 1.2.1

all:

# Include kernel specific configuration
kernel_name := $(shell sh -c 'uname -s 2>/dev/null || echo unknown')
-include contrib/config.make-$(kernel_name)

# Include setting from the configure script
-include config.make

prefix ?= $(HOME)
bindir ?= $(prefix)/bin
datarootdir ?= $(prefix)/share
sysconfdir ?= $(prefix)/etc
docdir ?= $(datarootdir)/doc
mandir ?= $(datarootdir)/man
# DESTDIR=

ifneq (,$(wildcard .git))
GITDESC	= $(subst tig-,,$(shell git describe))
WTDIRTY	= $(if $(shell git diff-index HEAD 2>/dev/null),-dirty)
VERSION	= $(GITDESC)$(WTDIRTY)
endif
ifdef DIST_VERSION
VERSION = $(DIST_VERSION)
endif

# Split the version "TAG-OFFSET-gSHA1-DIRTY" into "TAG OFFSET"
# and append 0 as a fallback offset for "exact" tagged versions.
RPM_VERLIST = $(filter-out g% dirty,$(subst -, ,$(VERSION))) 0
RPM_VERSION = $(word 1,$(RPM_VERLIST))
RPM_RELEASE = $(word 2,$(RPM_VERLIST))$(if $(WTDIRTY),.dirty)

LDLIBS ?= -lcurses
CFLAGS ?= -Wall -O2
DFLAGS	= -g -DDEBUG -Werror -O0
EXE	= src/tig
TOOLS	= test/test-graph tools/doc-gen
TXTDOC	= doc/tig.1.adoc doc/tigrc.5.adoc doc/manual.adoc NEWS.adoc README.adoc INSTALL.adoc
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

ASCIIDOC ?= asciidoc
ASCIIDOC_FLAGS = -aversion=$(VERSION) -asysconfdir=$(sysconfdir) -f doc/asciidoc.conf
XMLTO ?= xmlto
DOCBOOK2PDF ?= docbook2pdf

all: $(EXE) $(TOOLS)
all-debug: $(EXE) $(TOOLS)
all-debug: CFLAGS += $(DFLAGS)
doc: $(ALLDOC)
doc-man: $(MANDOC)
doc-html: $(HTMLDOC)

install: all
	@mkdir -p $(DESTDIR)$(bindir)
	install -p -m 0755 $(EXE) "$(DESTDIR)$(bindir)"
	@mkdir -p $(DESTDIR)$(sysconfdir)
	install -p -m 0444 tigrc "$(DESTDIR)$(sysconfdir)"

install-doc-man: doc-man
	mkdir -p $(DESTDIR)$(mandir)/man1 \
		 $(DESTDIR)$(mandir)/man5 \
		 $(DESTDIR)$(mandir)/man7
	for doc in $(MANDOC); do \
		sed 's#++SYSCONFDIR++#$(sysconfdir)#' < "$$doc" > "$$doc+"; \
		bdoc=$$(basename $$doc); \
		case "$$doc" in \
		*.1) install -p -m 0644 "$$doc+" "$(DESTDIR)$(mandir)/man1/$$bdoc" ;; \
		*.5) install -p -m 0644 "$$doc+" "$(DESTDIR)$(mandir)/man5/$$bdoc" ;; \
		*.7) install -p -m 0644 "$$doc+" "$(DESTDIR)$(mandir)/man7/$$bdoc" ;; \
		esac; \
		$(RM) "$$doc+"; \
	done

install-release-doc-man:
	GIT_INDEX_FILE=.tmp-doc-index git read-tree origin/release
	GIT_INDEX_FILE=.tmp-doc-index git checkout-index -f --prefix=./ $(MANDOC)
	rm -f .tmp-doc-index
	$(MAKE) install-doc-man

install-doc-html: doc-html
	mkdir -p $(DESTDIR)$(docdir)/tig
	for doc in $(HTMLDOC); do \
		sed 's#++SYSCONFDIR++#$(sysconfdir)#' < "$$doc" > "$$doc+"; \
		bdoc=$$(basename $$doc); \
		case "$$doc" in \
		*.html) install -p -m 0644 "$$doc+" "$(DESTDIR)$(docdir)/tig/$$bdoc" ;; \
		esac; \
		$(RM) "$$doc+"; \
	done

install-release-doc-html:
	GIT_INDEX_FILE=.tmp-doc-index git read-tree origin/release
	GIT_INDEX_FILE=.tmp-doc-index git checkout-index -f --prefix=./ $(HTMLDOC)
	rm -f .tmp-doc-index
	$(MAKE) install-doc-html

install-doc: install-doc-man install-doc-html
install-release-doc: install-release-doc-man install-release-doc-html

clean:
	$(RM) -r $(TARNAME) *.spec tig-*.tar.gz tig-*.tar.gz.md5 .deps
	$(RM) $(EXE) $(TOOLS) $(OBJS) core doc/*.xml src/builtin-config.c

distclean: clean
	$(RM) -r doc/manual.html-chunked autom4te.cache release-docs
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
	@for file in include/*.h src/*.c tools/*.c; do \
		grep -q '/* Copyright' "$$file" && \
			sed '0,/.*\*\//d' < "$$file" | \
			grep -v '/* vim: set' > "$$file.tmp"; \
		{ cat tools/header.h "$$file.tmp"; \
		  echo "/* vim: set ts=8 sw=8 noexpandtab: */"; } > "$$file"; \
		rm "$$file.tmp"; \
		echo "Updated $$file"; \
	done

update-docs: tools/doc-gen
	doc="doc/tigrc.5.adoc"; \
	sed -n '0,/ifndef::DOC_GEN_ACTIONS/p' < "$$doc" > "$$doc.gen"; \
	./tools/doc-gen actions >> "$$doc.gen"; \
	sed -n '/endif::DOC_GEN_ACTIONS/,$$p' < "$$doc" >> "$$doc.gen" ; \
	mv "$$doc.gen" "$$doc"

dist: configure tig.spec
	@mkdir -p $(TARNAME) && \
	cp Makefile tig.spec configure config.h.in aclocal.m4 $(TARNAME) && \
	sed -i "s/VERSION\s\+=\s\+[0-9]\+\([.][0-9]\+\)\+/VERSION	= $(VERSION)/" $(TARNAME)/Makefile
	git archive --format=tar --prefix=$(TARNAME)/ HEAD | \
	tar --delete $(TARNAME)/Makefile > $(TARNAME).tar && \
	tar rf $(TARNAME).tar `find $(TARNAME)/*` && \
	gzip -f -9 $(TARNAME).tar && \
	md5sum $(TARNAME).tar.gz > $(TARNAME).tar.gz.md5
	@$(RM) -r $(TARNAME)

rpm: dist
	rpmbuild -ta $(TARNAME).tar.gz

test: $(TOOLS)
	test/unit-test-graph.sh

# Other autoconf-related rules are hidden in config.make.in so that
# they don't confuse Make when we aren't actually using ./configure
configure: configure.ac acinclude.m4 tools/*.m4
	./autogen.sh

.PHONY: all all-debug doc doc-man doc-html install install-doc \
	install-doc-man install-doc-html clean spell-check dist rpm test

ifdef NO_MKSTEMPS
COMPAT_CPPFLAGS += -DNO_MKSTEMPS
COMPAT_OBJS += compat/mkstemps.o
endif

ifdef NO_SETENV
COMPAT_CPPFLAGS += -DNO_SETENV
COMPAT_OBJS += compat/setenv.o
endif

COMPAT_OBJS += compat/hashtab.o

override CPPFLAGS += $(COMPAT_CPPFLAGS)

TIG_OBJS = \
	src/tig.o \
	src/types.o \
	src/util.o \
	src/io.o \
	src/graph.o \
	src/refs.o \
	src/builtin-config.o \
	src/request.o \
	src/line.o \
	src/keys.o \
	src/repo.o \
	src/options.o \
	src/draw.o \
	src/display.o \
	src/view.o \
	src/parse.o \
	src/pager.o \
	src/log.o \
	src/diff.o \
	src/help.o \
	src/tree.o \
	src/blob.o \
	src/blame.o \
	src/branch.o \
	src/status.o \
	src/stage.o \
	src/main.o \
	$(COMPAT_OBJS)

src/tig: $(TIG_OBJS)

TEST_GRAPH_OBJS = test/test-graph.o src/util.o src/io.o src/graph.o $(COMPAT_OBJS)
test/test-graph: $(TEST_GRAPH_OBJS)

DOC_GEN_OBJS = tools/doc-gen.o src/types.o src/util.o src/request.o
tools/doc-gen: $(DOC_GEN_OBJS)

OBJS = $(sort $(TIG_OBJS) $(TEST_GRAPH_OBJS) $(DOC_GEN_OBJS))

DEPS_CFLAGS ?= -MMD -MP -MF .deps/$*.d

%: %.o
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

%.o: %.c
	@mkdir -p $(abspath .deps/$(*D))
	$(CC) -I. -Iinclude $(CFLAGS) $(DEPS_CFLAGS) $(CPPFLAGS) -c -o $@ $<

-include $(OBJS:%.o=.deps/%.d)

src/builtin-config.c: tigrc tools/make-builtin-config.sh
	tools/make-builtin-config.sh $< > $@

tig.spec: contrib/tig.spec.in
	sed -e 's/@@VERSION@@/$(RPM_VERSION)/g' \
	    -e 's/@@RELEASE@@/$(RPM_RELEASE)/g' < $< > $@

doc/manual.html: doc/manual.toc
doc/manual.html: ASCIIDOC_FLAGS += -ainclude-manual-toc
%.toc: %.adoc
	sed -n '/^\[\[/,/\(---\|~~~\)/p' < $< | while read line; do \
		case "$$line" in \
		"----"*)  echo ". <<$$ref>>"; ref= ;; \
		"~~~~"*)  echo "- <<$$ref>>"; ref= ;; \
		"[["*"]]") ref="$$line" ;; \
		*)	   ref="$$ref, $$line" ;; \
		esac; done | sed 's/\[\[\(.*\)\]\]/\1/' > $@

README.html: README.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article -a readme $<

INSTALL.html: INSTALL.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article $<

NEWS.html: NEWS.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article $<

doc/tigmanual.7: doc/manual.adoc

%.1.html : %.1.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d manpage $<

%.1.xml : %.1.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.5.html : %.5.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d manpage $<

%.5.xml : %.5.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.7.xml : %.7.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.html: ASCIIDOC_FLAGS += -adocext=html
%.html : %.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article -n $<

%.xml : %.adoc doc/asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d article $<

% : %.xml
	$(XMLTO) man -o doc $<

%.html-chunked : %.xml
	$(XMLTO) html -o $@ $<

%.pdf : %.xml
	$(DOCBOOK2PDF) -o doc $<
