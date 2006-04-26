LDFLAGS = -lcurses
CFLAGS	= -g '-DVERSION="$(VERSION)"' -Wall
PROGS	= tig
DOCS	= tig.1.txt tig.1 tig.1.html
VERSION	= $(shell git-describe)

all: $(PROGS)
docs: $(DOCS)

install: all
	for prog in $(PROGS); do \
		install $$prog $(HOME)/bin; \
	done

clean:
	rm -f $(PROGS) $(DOCS)

.PHONY: all docs install clean

tig: tig.c

tig.1.txt: tig.c
	sed -n '/^\/\*\*/,/\*\*\//p' < $< | \
	sed '/^[^*]\*\*/d' | \
	sed 's/\*\///;s/^[^*]*\* *//' > $@

%.1.html : %.1.txt
	asciidoc -b xhtml11 -d manpage -f asciidoc.conf $<

%.1.xml : %.1.txt
	asciidoc -b docbook -d manpage -f asciidoc.conf $<

%.1 : %.1.xml
	xmlto man $<
