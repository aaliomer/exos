#	$OpenBSD: Makefile,v 1.4 1996/08/22 01:36:51 deraadt Exp $
#	$NetBSD: Makefile,v 1.17 1995/10/24 22:18:34 jtc Exp $

PROG=	gzip
SRCS=	gzip.c zip.c deflate.c trees.c bits.c unzip.c inflate.c util.c \
	crypt.c lzw.c unlzw.c unlzh.c unpack.c getopt.c
MAN=	gzexe.1 gzip.1 zdiff.1 zforce.1 zgrep.1 zmore.1 znew.1
CFLAGS+=-DSTDC_HEADERS=1 -DHAVE_UNISTD_H=1 -DDIRENT=1

.if (${MACHINE_ARCH} == "m68k" || ${MACHINE_ARCH} == "i386")
SRCS+=	match.S
CFLAGS+=-DASMV
.endif

LDSTATIC= -static

MLINKS+= gzip.1 gunzip.1 gzip.1 gzcat.1 gzip.1 zcat.1
MLINKS+= zdiff.1 zcmp.1
#MLINKS+= gzip.1 uncompress.1
LINKS+= ${BINDIR}/gzip ${BINDIR}/gunzip
LINKS+= ${BINDIR}/gzip ${BINDIR}/gzcat
LINKS+= ${BINDIR}/gzip ${BINDIR}/zcat
LINKS+=	${BINDIR}/zdiff ${BINDIR}/zcmp
#LINKS+= ${BINDIR}/gzip ${BINDIR}/uncompress

afterinstall:
	install -c -o ${BINOWN} -g ${BINGRP} -m ${BINMODE} \
	   ${.CURDIR}/zforce ${.CURDIR}/gzexe ${.CURDIR}/znew \
	   ${.CURDIR}/zmore ${.CURDIR}/zdiff ${.CURDIR}/zgrep \
	   ${DESTDIR}${BINDIR}

.if (${MACHINE_ARCH} == "m68k" || ${MACHINE_ARCH} == "i386")
match.o: ${.CURDIR}/match.S
	$(CPP) -D__OpenBSD__ ${.CURDIR}/match.S >_match.s
	$(CC) -c _match.s
	mv _match.o match.o
	rm -f _match.s
.endif

.include <bsd.prog.mk>
