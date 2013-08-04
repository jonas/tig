## Makefile for tig

# The last tagged version. Can be overridden either by the version from
# git or from the value of the DIST_VERSION environment variable.
VERSION	= 1.1

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
PROGS	= tig
TESTS	= test-graph
SOURCE	= tig.c tig.h io.c io.h graph.c graph.h
TXTDOC	= tig.1.txt tigrc.5.txt manual.txt NEWS README INSTALL BUGS
MANDOC	= tig.1 tigrc.5 tigmanual.7
HTMLDOC = tig.1.html tigrc.5.html manual.html README.html NEWS.html
ALLDOC	= $(MANDOC) $(HTMLDOC) manual.html-chunked manual.pdf

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
ASCIIDOC_FLAGS = -aversion=$(VERSION) -asysconfdir=$(sysconfdir)
XMLTO ?= xmlto
DOCBOOK2PDF ?= docbook2pdf

all: $(PROGS) $(TESTS)
all-debug: $(PROGS) $(TESTS)
all-debug: CFLAGS += $(DFLAGS)
doc: $(ALLDOC)
doc-man: $(MANDOC)
doc-html: $(HTMLDOC)

install: all
	mkdir -p $(DESTDIR)$(bindir) && \
	for prog in $(PROGS); do \
		install -p -m 0755 "$$prog" "$(DESTDIR)$(bindir)"; \
	done

install-doc-man: doc-man
	mkdir -p $(DESTDIR)$(mandir)/man1 \
		 $(DESTDIR)$(mandir)/man5 \
		 $(DESTDIR)$(mandir)/man7
	for doc in $(MANDOC); do \
		sed 's#++SYSCONFDIR++#$(sysconfdir)#' < "$$doc" > "$$doc+"; \
		case "$$doc" in \
		*.1) install -p -m 0644 "$$doc+" "$(DESTDIR)$(mandir)/man1/$$doc" ;; \
		*.5) install -p -m 0644 "$$doc+" "$(DESTDIR)$(mandir)/man5/$$doc" ;; \
		*.7) install -p -m 0644 "$$doc+" "$(DESTDIR)$(mandir)/man7/$$doc" ;; \
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
		case "$$doc" in \
		*.html) install -p -m 0644 "$$doc+" "$(DESTDIR)$(docdir)/tig/$$doc" ;; \
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
	$(RM) -r $(TARNAME) *.spec tig-*.tar.gz tig-*.tar.gz.md5
	$(RM) $(PROGS) $(TESTS) core *.o compat/*.o *.xml

distclean: clean
	$(RM) -r manual.html-chunked autom4te.cache
	$(RM) *.toc $(ALLDOC) aclocal.m4 configure
	$(RM) config.h config.log config.make config.status config.h.in

spell-check:
	for file in $(TXTDOC) tig.c; do \
		aspell --lang=en --dont-backup \
		       --personal=./contrib/aspell.dict check $$file; \
	done

strip: $(PROGS)
	strip $(PROGS)

update-headers:
	@for file in *.[ch]; do \
		grep -q '/* Copyright' "$$file" && \
			sed '0,/.*\*\//d' < "$$file" | \
			grep -v '/* vim: set' > "$$file.tmp"; \
		{ cat contrib/header.h "$$file.tmp"; \
		  echo "/* vim: set ts=8 sw=8 noexpandtab: */"; } > "$$file"; \
		rm "$$file.tmp"; \
		echo "Updated $$file"; \
	done

dist: configure tig.spec
	@mkdir -p $(TARNAME) && \
	cp Makefile tig.spec configure config.h.in aclocal.m4 $(TARNAME) && \
	sed -i "s/VERSION\s=\s[0-9]\+[.][0-9]\+/VERSION	= $(VERSION)/" $(TARNAME)/Makefile
	git archive --format=tar --prefix=$(TARNAME)/ HEAD | \
	tar --delete $(TARNAME)/Makefile > $(TARNAME).tar && \
	tar rf $(TARNAME).tar `find $(TARNAME)/*` && \
	gzip -f -9 $(TARNAME).tar && \
	md5sum $(TARNAME).tar.gz > $(TARNAME).tar.gz.md5
	@$(RM) -r $(TARNAME)

rpm: dist
	rpmbuild -ta $(TARNAME).tar.gz

# Other autoconf-related rules are hidden in config.make.in so that
# they don't confuse Make when we aren't actually using ./configure
configure: configure.ac acinclude.m4 contrib/*.m4
	./autogen.sh

.PHONY: all all-debug doc doc-man doc-html install install-doc \
	install-doc-man install-doc-html clean spell-check dist rpm

ifdef NO_MKSTEMPS
COMPAT_CPPFLAGS += -DNO_MKSTEMPS
COMPAT_OBJS += compat/mkstemps.o
endif

override CPPFLAGS += $(COMPAT_CPPFLAGS)

graph.o: graph.c tig.h graph.h
io.o: io.c tig.h io.h
refs.o: refs.c tig.h io.h refs.h
test-graph.o: test-graph.c tig.h io.h graph.h
tig.o: tig.c tig.h io.h refs.h graph.h git.h

tig: tig.o io.o graph.o refs.o $(COMPAT_OBJS)
test-graph: io.o graph.o

# To check the above.
#
# NOTE: Assumes GCC, and that no local headers are conditionally
# included (with the exception of config.h, which we take care of in
# config.make).
show-deps:
	@echo "== without config.h =="
	$(CC) -MM *.c
	@echo "== with config.h =="
	$(CC) -DHAVE_CONFIG_H -MM *.c

tig.spec: contrib/tig.spec.in
	sed -e 's/@@VERSION@@/$(RPM_VERSION)/g' \
	    -e 's/@@RELEASE@@/$(RPM_RELEASE)/g' < $< > $@

manual.html: manual.toc
manual.toc: manual.txt
	sed -n '/^\[\[/,/\(---\|~~~\)/p' < $< | while read line; do \
		case "$$line" in \
		"----"*)  echo ". <<$$ref>>"; ref= ;; \
		"~~~~"*)  echo "- <<$$ref>>"; ref= ;; \
		"[["*"]]") ref="$$line" ;; \
		*)	   ref="$$ref, $$line" ;; \
		esac; done | sed 's/\[\[\(.*\)\]\]/\1/' > $@

README.html: README SITES INSTALL asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article -a readme $<

NEWS.html: NEWS asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article $<

tigmanual.7: manual.txt

%.1.html : %.1.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d manpage $<

%.1.xml : %.1.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.5.html : %.5.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d manpage $<

%.5.xml : %.5.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.7.xml : %.7.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d manpage $<

%.html : %.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b xhtml11 -d article -n $<

%.xml : %.txt asciidoc.conf
	$(ASCIIDOC) $(ASCIIDOC_FLAGS) -b docbook -d article $<

% : %.xml
	$(XMLTO) man $<

%.html-chunked : %.xml
	$(XMLTO) html -o $@ $<

%.pdf : %.xml
	$(DOCBOOK2PDF) $<
