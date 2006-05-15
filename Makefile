PREFIX	= $(HOME)
LDFLAGS = -lcurses
CFLAGS	= '-DVERSION="$(VERSION)"' -Wall
DFLAGS	= -g -DDEBUG
PROGS	= tig
DOCS	= tig.1.txt tig.1.html tig.1
VERSION	= $(shell git-describe)

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

.PHONY: all docs install clean

tig: tig.c

tig.1.txt: tig.c
	sed -n '/\/\*\*/,/\*\*\//p' < $< | \
	sed 's/.*\*\*\///' | \
	sed '/^[^*]*\*\*/d' | \
	sed 's/\*\///;s/^[^*]*\* *//' > $@

%.1.html : %.1.txt
	asciidoc -b xhtml11 -d manpage $<

%.1.xml : %.1.txt
	asciidoc -b docbook -d manpage $<

%.1 : %.1.xml
	xmlto man $<
