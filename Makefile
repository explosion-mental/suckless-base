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
	@echo CC $<
	@${CC} -c ${CFLAGS} $<

${OBJ}: config.h config.mk

xwindow: ${OBJ}
	@echo CC -o $@
	@${CC} -o $@ ${OBJ} ${LDFLAGS}

clean:
	@echo cleaning
	@rm -f xwindow ${OBJ} xwindow-${VERSION}.tar.gz

dist: clean
	@echo creating dist tarball
	@mkdir -p xwindow-${VERSION}
	@cp -R LICENSE Makefile config.mk config.def.h ${SRC} xwindow-${VERSION}
	@tar -cf xwindow-${VERSION}.tar xwindow-${VERSION}
	@gzip xwindow-${VERSION}.tar
	@rm -rf xwindow-${VERSION}

install: all
	@echo installing executable file to ${DESTDIR}${PREFIX}/bin
	@mkdir -p ${DESTDIR}${PREFIX}/bin
	@cp -f xwindow ${DESTDIR}${PREFIX}/bin
	@chmod 755 ${DESTDIR}${PREFIX}/bin/xwindow
	#@echo installing manual page to ${DESTDIR}${MANPREFIX}/man1
	#@mkdir -p ${DESTDIR}${MANPREFIX}/man1
	#@cp xwindow.1 ${DESTDIR}${MANPREFIX}/man1/xwindow.1
	#@chmod 644 ${DESTDIR}${MANPREFIX}/man1/xwindow.1

uninstall:
	@echo removing executable file from ${DESTDIR}${PREFIX}/bin
	@rm -f ${DESTDIR}${PREFIX}/bin/xwindow

.PHONY: all options clean dist install uninstall
