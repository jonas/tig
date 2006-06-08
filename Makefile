prefix	= $(HOME)
bindir= $(prefix)/bin
mandir = $(prefix)/man
# DESTDIR=

LDLIBS  = -lcurses
CFLAGS	= -Wall -O2
DFLAGS	= -g -DDEBUG -Werror
PROGS	= tig
DOCS	= tig.1.html tig.1 tigrc.5.html tigrc.5 \
	  manual.toc manual.html manual.html-chunked README.html \

ifneq (,$(wildcard .git))
VERSION = $(shell git-describe)
WTDIRTY = $(shell git-diff-index --name-only HEAD 2>/dev/null)
CFLAGS += '-DVERSION="$(VERSION)$(if $(WTDIRTY),-dirty)"'
endif

all: $(PROGS)
all-debug: $(PROGS)
all-debug: CFLAGS += $(DFLAGS)
doc: $(DOCS)

install: all
	for prog in $(PROGS); do \
		install $$prog $(DESTDIR)$(bindir); \
	done

install-doc: doc
	mkdir -p $(DESTDIR)$(mandir)/man1 $(DESTDIR)$(mandir)/man5
	for doc in $(DOCS); do \
		case "$$doc" in \
		*.1) install $$doc $(DESTDIR)$(mandir)/man1 ;; \
		*.5) install $$doc $(DESTDIR)$(mandir)/man5 ;; \
		esac \
	done

clean:
	rm -rf manual.html-chunked
	rm -f $(PROGS) $(DOCS) core

spell-check:
	aspell --lang=en --check tig.1.txt tigrc.5.txt manual.txt

strip: all
	strip $(PROGS)

.PHONY: all all-debug doc install install-doc clean spell-check

manual.toc: manual.txt
	sed -n '/^\[\[/,/\(---\|~~~\)/p' < $< | while read line; do \
		case "$$line" in \
		"-----"*)  echo ". <<$$ref>>"; ref= ;; \
		"~~~~~"*)  echo "- <<$$ref>>"; ref= ;; \
		"[["*"]]") ref="$$line" ;; \
		*)	   ref="$$ref, $$line" ;; \
		esac; done | sed 's/\[\[\(.*\)\]\]/\1/' > $@

tig: tig.c

README.html: README
	asciidoc -b xhtml11 -d article -a readme $<

%.1.html : %.1.txt
	asciidoc -b xhtml11 -d manpage $<

%.1.xml : %.1.txt
	asciidoc -b docbook -d manpage $<

%.1 : %.1.xml
	xmlto man $<

%.5.html : %.5.txt
	asciidoc -b xhtml11 -d manpage $<

%.5.xml : %.5.txt
	asciidoc -b docbook -d manpage $<

%.5 : %.5.xml
	xmlto man $<

%.html : %.txt
	asciidoc -b xhtml11 -d article -n $<

%.xml : %.txt
	asciidoc -b docbook -d article $<

%.html-chunked : %.xml
	xmlto html -o $@ $<
