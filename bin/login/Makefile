#	$OpenBSD: Makefile,v 1.13 1998/03/26 23:47:29 deraadt Exp $

PROG=	login
SRCS=	login.c failedlogin.c
LDADD=	-lutil
DPADD=	${LIBUTIL}
CFLAGS+= -Wall -Wno-unused

.include <bsd.own.mk>	# For SKEY, KERBEROS and KERBEROS5

.if (${SKEY} == "yes")
CFLAGS+=-DSKEY
LDADD+=	-lskey
DPADD+=	${LIBSKEY}
.endif

.if (${KERBEROS5} == "yes")
CFLAGS+= -DKERBEROS5
SRCS+=	k5login.c
LDADD+=	-lkrb5 -lcrypto
DPADD+=	${LIBKRB5} ${LIBCRYPTO}
.elif (${KERBEROS} == "yes")
CFLAGS+= -DKERBEROS
SRCS+=	klogin.c
LDADD+=	-lkrb -ldes -lkafs
DPADD+=	${LIBKRB} ${LIBDES} ${LIBKAFS}
.endif

BINOWN=	root
BINMODE=4555
INSTALLFLAGS=-fschg

.include <bsd.prog.mk>
