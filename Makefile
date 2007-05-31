prefix	= $(HOME)
bindir= $(prefix)/bin
mandir = $(prefix)/man
docdir = $(prefix)/share/doc
# DESTDIR=

# Get version either via git or from VERSION file
ifneq (,$(wildcard .git))
GITDESC = $(subst tig-,,$(shell git describe))
WTDIRTY = $(if $(shell git-diff-index HEAD 2>/dev/null),-dirty)
VERSION = $(GITDESC)$(WTDIRTY)
else
VERSION = $(shell test -f VERSION && cat VERSION || echo "unknown-version")
endif
RPM_VERSION = $(subst -,.,$(VERSION))

LDLIBS  = -lcurses
CFLAGS	= -Wall -O2 '-DVERSION="$(VERSION)"'
DFLAGS	= -g -DDEBUG -Werror
PROGS	= tig
DOCS_MAN	= tig.1 tigrc.5
DOCS_HTML	= tig.1.html tigrc.5.html \
		  manual.html manual.html-chunked \
		  README.html
DOCS	= $(DOCS_MAN) $(DOCS_HTML) \
	  manual.toc manual.pdf

TARNAME = tig-$(RPM_VERSION)

all: $(PROGS)
all-debug: $(PROGS)
all-debug: CFLAGS += $(DFLAGS)
doc: $(DOCS)
doc-man: $(DOCS_MAN)
doc-html: $(DOCS_HTML)

install: all
	mkdir -p $(DESTDIR)$(bindir) && \
	for prog in $(PROGS); do \
		install $$prog $(DESTDIR)$(bindir); \
	done

install-doc-man: doc-man
	mkdir -p $(DESTDIR)$(mandir)/man1 \
		 $(DESTDIR)$(mandir)/man5
	for doc in $(DOCS); do \
		case "$$doc" in \
		*.1) install $$doc $(DESTDIR)$(mandir)/man1 ;; \
		*.5) install $$doc $(DESTDIR)$(mandir)/man5 ;; \
		esac \
	done

install-doc-html: doc-html
	mkdir -p $(DESTDIR)$(docdir)/tig
	for doc in $(DOCS); do \
		case "$$doc" in \
		*.html) install $$doc $(DESTDIR)$(docdir)/tig ;; \
		esac \
	done

install-doc: install-doc-man install-doc-html

clean:
	rm -rf manual.html-chunked
	rm -f $(PROGS) $(DOCS) core *.xml
	rm -f *.spec
	rm -rf $(TARNAME)
	rm -f $(TARNAME).tar.gz

spell-check:
	aspell --lang=en --check tig.1.txt tigrc.5.txt manual.txt

strip: all
	strip $(PROGS)

dist: tig.spec
	git-archive --format=tar --prefix=$(TARNAME)/ HEAD > $(TARNAME).tar
	@mkdir -p $(TARNAME)
	@cp tig.spec $(TARNAME)
	tar rf $(TARNAME).tar $(TARNAME)/tig.spec
	@rm -rf $(TARNAME)
	gzip -f -9 $(TARNAME).tar

rpm: dist
	rpmbuild -ta $(TARNAME).tar.gz

.PHONY: all all-debug doc doc-man doc-html install install-doc install-doc-man install-doc-html clean spell-check dist rpm

tig.spec: tig.spec.in
	sed -e 's/@@VERSION@@/$(RPM_VERSION)/g' < $< > $@+
	mv $@+ $@

tig: tig.c

manual.html: manual.toc
manual.toc: manual.txt
	sed -n '/^\[\[/,/\(---\|~~~\)/p' < $< | while read line; do \
		case "$$line" in \
		"-----"*)  echo ". <<$$ref>>"; ref= ;; \
		"~~~~~"*)  echo "- <<$$ref>>"; ref= ;; \
		"[["*"]]") ref="$$line" ;; \
		*)	   ref="$$ref, $$line" ;; \
		esac; done | sed 's/\[\[\(.*\)\]\]/\1/' > $@

README.html: README
	asciidoc -b xhtml11 -d article -a readme $<

%.pdf : %.xml
	docbook2pdf $<

%.1.html : %.1.txt
	asciidoc -b xhtml11 -d manpage $<

%.1.xml : %.1.txt
	asciidoc -b docbook -d manpage -aversion=$(VERSION) $<

%.1 : %.1.xml
	xmlto -m manpage.xsl man $<

%.5.html : %.5.txt
	asciidoc -b xhtml11 -d manpage $<

%.5.xml : %.5.txt
	asciidoc -b docbook -d manpage -aversion=$(VERSION) $<

%.5 : %.5.xml
	xmlto -m manpage.xsl man $<

%.html : %.txt
	asciidoc -b xhtml11 -d article -n $<

%.xml : %.txt
	asciidoc -b docbook -d article $<

%.html-chunked : %.xml
	xmlto html -o $@ $<

# Maintainer stuff
sync-docs:
	cg switch release
	-cg merge -n master
	cg commit -m "Merge with master"
	make doc
	cg commit -m "Sync docs"
	cg switch master
