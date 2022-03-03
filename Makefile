# xwindow - X11 windows made "alla" suckless
# See LICENSE file for copyright and license details.

include config.mk

SRC = xwindow.c drw.c util.c
OBJ = ${SRC:.c=.o}

all: options xwindow

options:
	@echo xwindow build options:
	@echo "CFLAGS   = ${CFLAGS}"
	@echo "LDFLAGS  = ${LDFLAGS}"
	@echo "CC       = ${CC}"

config.h:
	cp config.def.h config.h

.c.o:
	${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

xwindow: ${OBJ}
	${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	rm -f xwindow ${OBJ} xwindow-${VERSION}.tar.gz

dist: clean
	mkdir -p xwindow-${VERSION}
	cp -R LICENSE Makefile config.mk config.def.h ${SRC} xwindow-${VERSION}
	tar -cf xwindow-${VERSION}.tar xwindow-${VERSION}
	gzip xwindow-${VERSION}.tar
	rm -rf xwindow-${VERSION}

install: all
	mkdir -p ${DESTDIR}${PREFIX}/bin
	cp -f xwindow ${DESTDIR}${PREFIX}/bin
	chmod 755 ${DESTDIR}${PREFIX}/bin/xwindow

uninstall:
	rm -f ${DESTDIR}${PREFIX}/bin/xwindow

.PHONY: all options clean dist install uninstall
