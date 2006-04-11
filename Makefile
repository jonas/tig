LDFLAGS = -lcurses
CFLAGS	= -g

all: cgit

install: all
	install cgit $(HOME)/bin

clean:
	rm -f cgit

cgit: cgit.c
