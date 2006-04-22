LDFLAGS = -lcurses
CFLAGS	= -g
PROGS	= cgit
DOCS	= cgit.1.txt cgit.1 cgit.1.html

all: $(PROGS)
docs: $(DOCS)

install: all
	for prog in $(PROGS); do \
		install $$prog $(HOME)/bin; \
	done

clean:
	rm -f $(PROGS) $(DOCS)

cgit: cgit.c

cgit.1.txt: cgit.c
	sed -n '/\*\*/,/\*\*/p' < $< | \
	sed '/\*\*/d' | \
	sed -n 's/^ \* *//p' > $@

%.1.html : %.1.txt
	asciidoc -b xhtml11 -d manpage -f asciidoc.conf $<

%.1.xml : %.1.txt
	asciidoc -b docbook -d manpage -f asciidoc.conf $<

%.1 : %.1.xml
	xmlto man $<
