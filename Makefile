PREFIX	= $(HOME)
LDLIBS  = -lcurses
CFLAGS	= -Wall -O2
DFLAGS	= -g -DDEBUG -Werror
PROGS	= tig
DOCS	= tig.1.txt tig.1.html tig.1 README.html

ifneq (,$(wildcard .git))
VERSION = $(shell git-describe)
WTDIRTY = $(shell git-diff-index --name-only HEAD 2>/dev/null)
CFLAGS += '-DVERSION="$(VERSION)$(if $(WTDIRTY),-dirty)"'
endif

all: $(PROGS)
all-debug: $(PROGS)
all-debug: CFLAGS += $(DFLAGS)
docs: $(DOCS)

install: all
	for prog in $(PROGS); do \
		install $$prog $(PREFIX)/bin; \
	done

install-docs: docs
	for doc in $(DOCS); do \
		case "$$doc" in \
		*.1) install $$doc $(PREFIX)/man/man1 ;; \
		esac \
	done

clean:
	rm -f $(PROGS) $(DOCS) core

spell-check:
	aspell --lang=en --check tig.1.txt

.PHONY: all docs install clean

tig: tig.c

tig.1.txt: tig.c
	sed -n '/\/\*\*/,/\*\*\//p' < $< | \
	sed 's/.*\*\*\///' | \
	sed '/^[^*]*\*\*/d' | \
	sed 's/\*\///;s/^[^*]*\* *//' > $@

README.html: README
	asciidoc -b xhtml11 -d article -f web.conf $<

%.1.html : %.1.txt
	asciidoc -b xhtml11 -d manpage $<

%.1.xml : %.1.txt
	asciidoc -b docbook -d manpage $<

%.1 : %.1.xml
	xmlto man $<
