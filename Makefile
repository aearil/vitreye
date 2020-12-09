PREFIX = /usr/local
MANPREFIX = $(PREFIX)/share/man
CFLAGS   = -std=c99 -pedantic -Wall -Wextra -g
LDFLAGS  =
LIBS = -lSDL2 -lpng -ljpeg

vitreye: vitreye.o util.o
	cc $(LDFLAGS) $(LIBS) $^ -o $@

%.o: %.c util.h
	cc $(CFLAGS) -c $< -o $@

install: vitreye
	mkdir -p "$(DESTDIR)$(PREFIX)/bin"
	cp -f $< "$(DESTDIR)$(PREFIX)/bin"
	chmod 755 "$(DESTDIR)$(PREFIX)/bin/$<"
	mkdir -p "$(DESTDIR)$(MANPREFIX)/man1"
	cp -f vitreye.1 "$(DESTDIR)$(MANPREFIX)/man1"
	chmod 644 "$(DESTDIR)$(MANPREFIX)/man1/vitreye.1"

clean:
	rm -f ./vitreye
	rm -f ./*.o

.PHONY: clean
