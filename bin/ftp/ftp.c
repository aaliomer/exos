/*	$OpenBSD: ftp.c,v 1.29 1998/07/07 17:26:41 art Exp $	*/
/*	$NetBSD: ftp.c,v 1.27 1997/08/18 10:20:23 lukem Exp $	*/

/*
 * Copyright (c) 1985, 1989, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef lint
#if 0
static char sccsid[] = "@(#)ftp.c	8.6 (Berkeley) 10/27/94";
#else
static char rcsid[] = "$OpenBSD: ftp.c,v 1.29 1998/07/07 17:26:41 art Exp $";
#endif
#endif /* not lint */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <arpa/ftp.h>
#include <arpa/telnet.h>

#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>
#ifdef __STDC__
#include <stdarg.h>
#else
#include <varargs.h>
#endif

#include "ftp_var.h"

struct	sockaddr_in hisctladdr;
struct	sockaddr_in data_addr;
int	data = -1;
int	abrtflag = 0;
jmp_buf	ptabort;
int	ptabflg;
int	ptflag = 0;
struct	sockaddr_in myctladdr;
off_t	restart_point = 0;


FILE	*cin, *cout;

char *
hookup(host, port)
	const char *host;
	in_port_t port;
{
	struct hostent *hp = NULL;
	int s, len, tos;
	static char hostnamebuf[MAXHOSTNAMELEN];

	memset((void *)&hisctladdr, 0, sizeof(hisctladdr));
	if (inet_aton(host, &hisctladdr.sin_addr) != 0) {
		hisctladdr.sin_family = AF_INET;
		(void)strncpy(hostnamebuf, host, sizeof(hostnamebuf) - 1);
		hostnamebuf[sizeof(hostnamebuf) - 1] = '\0';
	} else {
		hp = gethostbyname(host);
		if (hp == NULL) {
			warnx("%s: %s", host, hstrerror(h_errno));
			code = -1;
			return ((char *) 0);
		}
		hisctladdr.sin_family = hp->h_addrtype;
		memcpy(&hisctladdr.sin_addr, hp->h_addr_list[0],
		    (size_t)hp->h_length);
		(void)strncpy(hostnamebuf, hp->h_name, sizeof(hostnamebuf) - 1);
		hostnamebuf[sizeof(hostnamebuf) - 1] = '\0';
	}
	hostname = hostnamebuf;
	s = socket(hisctladdr.sin_family, SOCK_STREAM, 0);
	if (s < 0) {
		warn("socket");
		code = -1;
		return (0);
	}
	hisctladdr.sin_port = port;
	while (connect(s, (struct sockaddr *)&hisctladdr,
			sizeof(hisctladdr)) < 0) {
		if (errno == EINTR)
			continue;
		if (hp && hp->h_addr_list[1]) {
			int oerrno = errno;
			char *ia;

			ia = inet_ntoa(hisctladdr.sin_addr);
			errno = oerrno;
			warn("connect to address %s", ia);
			hp->h_addr_list++;
			memcpy(&hisctladdr.sin_addr, hp->h_addr_list[0],
			    (size_t)hp->h_length);
			fprintf(ttyout, "Trying %s...\n",
			    inet_ntoa(hisctladdr.sin_addr));
			(void)close(s);
			s = socket(hisctladdr.sin_family, SOCK_STREAM, 0);
			if (s < 0) {
				warn("socket");
				code = -1;
				return (0);
			}
			continue;
		}
		warn("connect");
		code = -1;
		goto bad;
	}
	len = sizeof(myctladdr);
	if (getsockname(s, (struct sockaddr *)&myctladdr, &len) < 0) {
		warn("getsockname");
		code = -1;
		goto bad;
	}
#ifdef IP_TOS
	tos = IPTOS_LOWDELAY;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int)) < 0)
		warn("setsockopt TOS (ignored)");
#endif
	cin = fdopen(s, "r");
	cout = fdopen(s, "w");
	if (cin == NULL || cout == NULL) {
		warnx("fdopen failed.");
		if (cin)
			(void)fclose(cin);
		if (cout)
			(void)fclose(cout);
		code = -1;
		goto bad;
	}
	if (verbose)
		fprintf(ttyout, "Connected to %s.\n", hostname);
	if (getreply(0) > 2) { 	/* read startup message from server */
		if (cin)
			(void)fclose(cin);
		if (cout)
			(void)fclose(cout);
		code = -1;
		goto bad;
	}
#ifdef SO_OOBINLINE
	{
	int on = 1;

	if (setsockopt(s, SOL_SOCKET, SO_OOBINLINE, (char *)&on, sizeof(on))
		< 0 && debug) {
			warn("setsockopt");
		}
	}
#endif /* SO_OOBINLINE */

	return (hostname);
bad:
	(void)close(s);
	return ((char *)0);
}

void
cmdabort(notused)
	int notused;
{

	alarmtimer(0);
	putc('\n', ttyout);
	(void)fflush(ttyout);
	abrtflag++;
	if (ptflag)
		longjmp(ptabort, 1);
}

/*VARARGS*/
int
#ifdef __STDC__
command(const char *fmt, ...)
#else
command(va_alist)
	va_dcl
#endif
{
	va_list ap;
	int r;
	sig_t oldintr;
#ifndef __STDC__
	const char *fmt;
#endif

	abrtflag = 0;
	if (debug) {
		fputs("---> ", ttyout);
#ifdef __STDC__
		va_start(ap, fmt);
#else
		va_start(ap);
		fmt = va_arg(ap, const char *);
#endif
		if (strncmp("PASS ", fmt, 5) == 0)
			fputs("PASS XXXX", ttyout);
		else if (strncmp("ACCT ", fmt, 5) == 0)
			fputs("ACCT XXXX", ttyout);
		else
			vfprintf(ttyout, fmt, ap);
		va_end(ap);
		putc('\n', ttyout);
		(void)fflush(ttyout);
	}
	if (cout == NULL) {
		warnx("No control connection for command.");
		code = -1;
		return (0);
	}
	oldintr = signal(SIGINT, cmdabort);
#ifdef __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
	fmt = va_arg(ap, char *);
#endif
	vfprintf(cout, fmt, ap);
	va_end(ap);
	fputs("\r\n", cout);
	(void)fflush(cout);
	cpend = 1;
	r = getreply(!strcmp(fmt, "QUIT"));
	if (abrtflag && oldintr != SIG_IGN)
		(*oldintr)(SIGINT);
	(void)signal(SIGINT, oldintr);
	return (r);
}

char reply_string[BUFSIZ];		/* first line of previous reply */

int
getreply(expecteof)
	int expecteof;
{
	char current_line[BUFSIZ];	/* last line of previous reply */
	int c, n, line;
	int dig;
	int originalcode = 0, continuation = 0;
	sig_t oldintr;
	int pflag = 0;
	char *cp, *pt = pasv;

	memset(current_line, 0, sizeof(current_line));
	oldintr = signal(SIGINT, cmdabort);
	for (line = 0 ;; line++) {
		dig = n = code = 0;
		cp = current_line;
		while ((c = fgetc(cin)) != '\n') {
			if (c == IAC) {     /* handle telnet commands */
				switch (c = fgetc(cin)) {
				case WILL:
				case WONT:
					c = fgetc(cin);
					fprintf(cout, "%c%c%c", IAC, DONT, c);
					(void)fflush(cout);
					break;
				case DO:
				case DONT:
					c = fgetc(cin);
					fprintf(cout, "%c%c%c", IAC, WONT, c);
					(void)fflush(cout);
					break;
				default:
					break;
				}
				continue;
			}
			dig++;
			if (c == EOF) {
				if (expecteof) {
					(void)signal(SIGINT, oldintr);
					code = 221;
					return (0);
				}
				lostpeer();
				if (verbose) {
					fputs(
"421 Service not available, remote server has closed connection.\n", ttyout);
					(void)fflush(ttyout);
				}
				code = 421;
				return (4);
			}
			if (c != '\r' && (verbose > 0 ||
			    ((verbose > -1 && n == '5' && dig > 4)) &&
			    (((!n && c < '5') || (n && n < '5'))
			     || !retry_connect))) {
				if (proxflag &&
				   (dig == 1 || (dig == 5 && verbose == 0)))
					fprintf(ttyout, "%s:", hostname);
				(void)putc(c, ttyout);
			}
			if (dig < 4 && isdigit(c))
				code = code * 10 + (c - '0');
			if (!pflag && code == 227)
				pflag = 1;
			if (dig > 4 && pflag == 1 && isdigit(c))
				pflag = 2;
			if (pflag == 2) {
				if (c != '\r' && c != ')')
					*pt++ = c;
				else {
					*pt = '\0';
					pflag = 3;
				}
			}
			if (dig == 4 && c == '-') {
				if (continuation)
					code = 0;
				continuation++;
			}
			if (n == 0)
				n = c;
			if (cp < &current_line[sizeof(current_line) - 1])
				*cp++ = c;
		}
		if (verbose > 0 || ((verbose > -1 && n == '5') &&
		    (n < '5' || !retry_connect))) {
			(void)putc(c, ttyout);
			(void)fflush (ttyout);
		}
		if (line == 0) {
			size_t len = cp - current_line;

			if (len > sizeof(reply_string))
				len = sizeof(reply_string);

			(void)strncpy(reply_string, current_line, len);
			reply_string[len] = '\0';
		}
		if (continuation && code != originalcode) {
			if (originalcode == 0)
				originalcode = code;
			continue;
		}
		*cp = '\0';
		if (n != '1')
			cpend = 0;
		(void)signal(SIGINT, oldintr);
		if (code == 421 || originalcode == 421)
			lostpeer();
		if (abrtflag && oldintr != cmdabort && oldintr != SIG_IGN)
			(*oldintr)(SIGINT);
		return (n - '0');
	}
}

int
empty(mask, sec)
	fd_set *mask;
	int sec;
{
	struct timeval t;

	t.tv_sec = (long) sec;
	t.tv_usec = 0;
	return (select(32, mask, (fd_set *) 0, (fd_set *) 0, &t));
}

jmp_buf	sendabort;

void
abortsend(notused)
	int notused;
{

	alarmtimer(0);
	mflag = 0;
	abrtflag = 0;
	fputs("\nsend aborted\nwaiting for remote to finish abort.\n", ttyout);
	(void)fflush(ttyout);
	longjmp(sendabort, 1);
}

void
sendrequest(cmd, local, remote, printnames)
	const char *cmd, *local, *remote;
	int printnames;
{
	struct stat st;
	int c, d;
	FILE *fin, *dout;
	int (*closefunc) __P((FILE *));
	sig_t oldinti, oldintr, oldintp;
	volatile off_t hashbytes;
	char *lmode, buf[BUFSIZ], *bufp;
	int oprogress;

#ifdef __GNUC__				/* XXX: to shut up gcc warnings */
	(void)&fin;
	(void)&dout;
	(void)&closefunc;
	(void)&oldinti;
	(void)&oldintr;
	(void)&oldintp;
	(void)&lmode;
#endif

	hashbytes = mark;
	direction = "sent";
	dout = NULL;
	bytes = 0;
	filesize = -1;
	oprogress = progress;
	if (verbose && printnames) {
		if (local && *local != '-')
			fprintf(ttyout, "local: %s ", local);
		if (remote)
			fprintf(ttyout, "remote: %s\n", remote);
	}
	if (proxy) {
		proxtrans(cmd, local, remote);
		return;
	}
	if (curtype != type)
		changetype(type, 0);
	closefunc = NULL;
	oldintr = NULL;
	oldintp = NULL;
	oldinti = NULL;
	lmode = "w";
	if (setjmp(sendabort)) {
		while (cpend) {
			(void)getreply(0);
		}
		if (data >= 0) {
			(void)close(data);
			data = -1;
		}
		if (oldintr)
			(void)signal(SIGINT, oldintr);
		if (oldintp)
			(void)signal(SIGPIPE, oldintp);
		if (oldinti)
			(void)signal(SIGINFO, oldinti);
		progress = oprogress;
		code = -1;
		return;
	}
	oldintr = signal(SIGINT, abortsend);
	oldinti = signal(SIGINFO, psummary);
	if (strcmp(local, "-") == 0) {
		fin = stdin;
		progress = 0;
	} else if (*local == '|') {
		oldintp = signal(SIGPIPE, SIG_IGN);
		fin = popen(local + 1, "r");
		if (fin == NULL) {
			warn("%s", local + 1);
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGPIPE, oldintp);
			(void)signal(SIGINFO, oldinti);
			code = -1;
			return;
		}
		progress = 0;
		closefunc = pclose;
	} else {
		fin = fopen(local, "r");
		if (fin == NULL) {
			warn("local: %s", local);
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			code = -1;
			return;
		}
		closefunc = fclose;
		if (fstat(fileno(fin), &st) < 0 ||
		    (st.st_mode & S_IFMT) != S_IFREG) {
			fprintf(ttyout, "%s: not a plain file.\n", local);
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			fclose(fin);
			code = -1;
			return;
		}
		filesize = st.st_size;
	}
	if (initconn()) {
		(void)signal(SIGINT, oldintr);
		(void)signal(SIGINFO, oldinti);
		if (oldintp)
			(void)signal(SIGPIPE, oldintp);
		code = -1;
		progress = oprogress;
		if (closefunc != NULL)
			(*closefunc)(fin);
		return;
	}
	if (setjmp(sendabort))
		goto abort;

	if (restart_point &&
	    (strcmp(cmd, "STOR") == 0 || strcmp(cmd, "APPE") == 0)) {
		int rc;

		rc = -1;
		switch (curtype) {
		case TYPE_A:
			rc = fseek(fin, (long) restart_point, SEEK_SET);
			break;
		case TYPE_I:
		case TYPE_L:
			rc = lseek(fileno(fin), restart_point, SEEK_SET);
			break;
		}
		if (rc < 0) {
			warn("local: %s", local);
			restart_point = 0;
			progress = oprogress;
			if (closefunc != NULL)
				(*closefunc)(fin);
			return;
		}
		if (command("REST %ld", (long) restart_point)
			!= CONTINUE) {
			restart_point = 0;
			progress = oprogress;
			if (closefunc != NULL)
				(*closefunc)(fin);
			return;
		}
		restart_point = 0;
		lmode = "r+w";
	}
	if (remote) {
		if (command("%s %s", cmd, remote) != PRELIM) {
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			progress = oprogress;
			if (oldintp)
				(void)signal(SIGPIPE, oldintp);
			if (closefunc != NULL)
				(*closefunc)(fin);
			return;
		}
	} else
		if (command("%s", cmd) != PRELIM) {
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			progress = oprogress;
			if (oldintp)
				(void)signal(SIGPIPE, oldintp);
			if (closefunc != NULL)
				(*closefunc)(fin);
			return;
		}
	dout = dataconn(lmode);
	if (dout == NULL)
		goto abort;
	progressmeter(-1);
	oldintp = signal(SIGPIPE, SIG_IGN);
	switch (curtype) {

	case TYPE_I:
	case TYPE_L:
		errno = d = 0;
		while ((c = read(fileno(fin), buf, sizeof(buf))) > 0) {
			bytes += c;
			for (bufp = buf; c > 0; c -= d, bufp += d)
				if ((d = write(fileno(dout), bufp, (size_t)c))
				    <= 0)
					break;
			if (hash && (!progress || filesize < 0) ) {
				while (bytes >= hashbytes) {
					(void)putc('#', ttyout);
					hashbytes += mark;
				}
				(void)fflush(ttyout);
			}
		}
		if (hash && (!progress || filesize < 0) && bytes > 0) {
			if (bytes < mark)
				(void)putc('#', ttyout);
			(void)putc('\n', ttyout);
			(void)fflush(ttyout);
		}
		if (c < 0)
			warn("local: %s", local);
		if (d < 0) {
			if (errno != EPIPE)
				warn("netout");
			bytes = -1;
		}
		break;

	case TYPE_A:
		while ((c = fgetc(fin)) != EOF) {
			if (c == '\n') {
				while (hash && (!progress || filesize < 0) &&
				    (bytes >= hashbytes)) {
					(void)putc('#', ttyout);
					(void)fflush(ttyout);
					hashbytes += mark;
				}
				if (ferror(dout))
					break;
				(void)putc('\r', dout);
				bytes++;
			}
			(void)putc(c, dout);
			bytes++;
#if 0	/* this violates RFC */
			if (c == '\r') {
				(void)putc('\0', dout);
				bytes++;
			}
#endif
		}
		if (hash && (!progress || filesize < 0)) {
			if (bytes < hashbytes)
				(void)putc('#', ttyout);
			(void)putc('\n', ttyout);
			(void)fflush(ttyout);
		}
		if (ferror(fin))
			warn("local: %s", local);
		if (ferror(dout)) {
			if (errno != EPIPE)
				warn("netout");
			bytes = -1;
		}
		break;
	}
	progressmeter(1);
	progress = oprogress;
	if (closefunc != NULL)
		(*closefunc)(fin);
	(void)fclose(dout);
	(void)getreply(0);
	(void)signal(SIGINT, oldintr);
	(void)signal(SIGINFO, oldinti);
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	if (bytes > 0)
		ptransfer(0);
	return;
abort:
	(void)signal(SIGINT, oldintr);
	(void)signal(SIGINFO, oldinti);
	progress = oprogress;
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	if (!cpend) {
		code = -1;
		return;
	}
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (dout)
		(void)fclose(dout);
	(void)getreply(0);
	code = -1;
	if (closefunc != NULL && fin != NULL)
		(*closefunc)(fin);
	if (bytes > 0)
		ptransfer(0);
}

jmp_buf	recvabort;

void
abortrecv(notused)
	int notused;
{

	alarmtimer(0);
	mflag = 0;
	abrtflag = 0;
	fputs("\nreceive aborted\nwaiting for remote to finish abort.\n", ttyout);
	(void)fflush(ttyout);
	longjmp(recvabort, 1);
}

void
recvrequest(cmd, local, remote, lmode, printnames, ignorespecial)
	const char *cmd, *local, *remote, *lmode;
	int printnames, ignorespecial;
{
	FILE *fout, *din;
	int (*closefunc) __P((FILE *));
	sig_t oldinti, oldintr, oldintp;
	int c, d;
	volatile int is_retr, tcrflag, bare_lfs;
	static size_t bufsize;
	static char *buf;
	volatile off_t hashbytes;
	struct stat st;
	time_t mtime;
	int oprogress;
	int opreserve;

#ifdef __GNUC__				/* XXX: to shut up gcc warnings */
	(void)&local;
	(void)&fout;
	(void)&din;
	(void)&closefunc;
	(void)&oldinti;
	(void)&oldintr;
	(void)&oldintp;
#endif

	fout = NULL;
	din = NULL;
	oldinti = NULL;
	hashbytes = mark;
	direction = "received";
	bytes = 0;
	bare_lfs = 0;
	filesize = -1;
	oprogress = progress;
	opreserve = preserve;
	is_retr = strcmp(cmd, "RETR") == 0;
	if (is_retr && verbose && printnames) {
		if (local && (ignorespecial || *local != '-'))
			fprintf(ttyout, "local: %s ", local);
		if (remote)
			fprintf(ttyout, "remote: %s\n", remote);
	}
	if (proxy && is_retr) {
		proxtrans(cmd, local, remote);
		return;
	}
	closefunc = NULL;
	oldintr = NULL;
	oldintp = NULL;
	tcrflag = !crflag && is_retr;
	if (setjmp(recvabort)) {
		while (cpend) {
			(void)getreply(0);
		}
		if (data >= 0) {
			(void)close(data);
			data = -1;
		}
		if (oldintr)
			(void)signal(SIGINT, oldintr);
		if (oldinti)
			(void)signal(SIGINFO, oldinti);
		progress = oprogress;
		preserve = opreserve;
		code = -1;
		return;
	}
	oldintr = signal(SIGINT, abortrecv);
	oldinti = signal(SIGINFO, psummary);
	if (ignorespecial || (strcmp(local, "-") && *local != '|')) {
		if (access(local, W_OK) < 0) {
			char *dir = strrchr(local, '/');

			if (errno != ENOENT && errno != EACCES) {
				warn("local: %s", local);
				(void)signal(SIGINT, oldintr);
				(void)signal(SIGINFO, oldinti);
				code = -1;
				return;
			}
			if (dir != NULL)
				*dir = 0;
			d = access(dir == local ? "/" : dir ? local : ".", W_OK);
			if (dir != NULL)
				*dir = '/';
			if (d < 0) {
				warn("local: %s", local);
				(void)signal(SIGINT, oldintr);
				(void)signal(SIGINFO, oldinti);
				code = -1;
				return;
			}
			if (!runique && errno == EACCES &&
			    chmod(local, (S_IRUSR|S_IWUSR)) < 0) {
				warn("local: %s", local);
				(void)signal(SIGINT, oldintr);
				(void)signal(SIGINFO, oldinti);
				code = -1;
				return;
			}
			if (runique && errno == EACCES &&
			   (local = gunique(local)) == NULL) {
				(void)signal(SIGINT, oldintr);
				(void)signal(SIGINFO, oldinti);
				code = -1;
				return;
			}
		}
		else if (runique && (local = gunique(local)) == NULL) {
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			code = -1;
			return;
		}
	}
	if (!is_retr) {
		if (curtype != TYPE_A)
			changetype(TYPE_A, 0);
	} else {
		if (curtype != type)
			changetype(type, 0);
		filesize = remotesize(remote, 0);
	}
	if (initconn()) {
		(void)signal(SIGINT, oldintr);
		(void)signal(SIGINFO, oldinti);
		code = -1;
		return;
	}
	if (setjmp(recvabort))
		goto abort;
	if (is_retr && restart_point &&
	    command("REST %ld", (long) restart_point) != CONTINUE)
		return;
	if (remote) {
		if (command("%s %s", cmd, remote) != PRELIM) {
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			return;
		}
	} else {
		if (command("%s", cmd) != PRELIM) {
			(void)signal(SIGINT, oldintr);
			(void)signal(SIGINFO, oldinti);
			return;
		}
	}
	din = dataconn("r");
	if (din == NULL)
		goto abort;
	if (!ignorespecial && strcmp(local, "-") == 0) {
		fout = stdout;
		progress = 0;
		preserve = 0;
	} else if (!ignorespecial && *local == '|') {
		oldintp = signal(SIGPIPE, SIG_IGN);
		fout = popen(local + 1, "w");
		if (fout == NULL) {
			warn("%s", local+1);
			goto abort;
		}
		progress = 0;
		preserve = 0;
		closefunc = pclose;
	} else {
		fout = fopen(local, lmode);
		if (fout == NULL) {
			warn("local: %s", local);
			goto abort;
		}
		closefunc = fclose;
	}
	if (fstat(fileno(fout), &st) < 0 || st.st_blksize == 0)
		st.st_blksize = BUFSIZ;
	if (st.st_blksize > bufsize) {
		if (buf)
			(void)free(buf);
		buf = malloc((unsigned)st.st_blksize);
		if (buf == NULL) {
			warn("malloc");
			bufsize = 0;
			goto abort;
		}
		bufsize = st.st_blksize;
	}
	if ((st.st_mode & S_IFMT) != S_IFREG) {
		progress = 0;
		preserve = 0;
	}
	progressmeter(-1);
	switch (curtype) {

	case TYPE_I:
	case TYPE_L:
		if (restart_point &&
		    lseek(fileno(fout), restart_point, SEEK_SET) < 0) {
			warn("local: %s", local);
			progress = oprogress;
			preserve = opreserve;
			if (closefunc != NULL)
				(*closefunc)(fout);
			return;
		}
		errno = d = 0;
		while ((c = read(fileno(din), buf, bufsize)) > 0) {
			if ((d = write(fileno(fout), buf, (size_t)c)) != c)
				break;
			bytes += c;
			if (hash && (!progress || filesize < 0)) {
				while (bytes >= hashbytes) {
					(void)putc('#', ttyout);
					hashbytes += mark;
				}
				(void)fflush(ttyout);
			}
		}
		if (hash && (!progress || filesize < 0) && bytes > 0) {
			if (bytes < mark)
				(void)putc('#', ttyout);
			(void)putc('\n', ttyout);
			(void)fflush(ttyout);
		}
		if (c < 0) {
			if (errno != EPIPE)
				warn("netin");
			bytes = -1;
		}
		if (d < c) {
			if (d < 0)
				warn("local: %s", local);
			else
				warnx("%s: short write", local);
		}
		break;

	case TYPE_A:
		if (restart_point) {
			int i, n, ch;

			if (fseek(fout, 0L, SEEK_SET) < 0)
				goto done;
			n = restart_point;
			for (i = 0; i++ < n;) {
				if ((ch = fgetc(fout)) == EOF)
					goto done;
				if (ch == '\n')
					i++;
			}
			if (fseek(fout, 0L, SEEK_CUR) < 0) {
done:
				warn("local: %s", local);
				progress = oprogress;
				preserve = opreserve;
				if (closefunc != NULL)
					(*closefunc)(fout);
				return;
			}
		}
		while ((c = fgetc(din)) != EOF) {
			if (c == '\n')
				bare_lfs++;
			while (c == '\r') {
				while (hash && (!progress || filesize < 0) &&
				    (bytes >= hashbytes)) {
					(void)putc('#', ttyout);
					(void)fflush(ttyout);
					hashbytes += mark;
				}
				bytes++;
				if ((c = fgetc(din)) != '\n' || tcrflag) {
					if (ferror(fout))
						goto break2;
					(void)putc('\r', fout);
					if (c == '\0') {
						bytes++;
						goto contin2;
					}
					if (c == EOF)
						goto contin2;
				}
			}
			(void)putc(c, fout);
			bytes++;
	contin2:	;
		}
break2:
		if (bare_lfs) {
			fprintf(ttyout,
"WARNING! %d bare linefeeds received in ASCII mode.\n", bare_lfs);
			fputs("File may not have transferred correctly.\n",
			    ttyout);
		}
		if (hash && (!progress || filesize < 0)) {
			if (bytes < hashbytes)
				(void)putc('#', ttyout);
			(void)putc('\n', ttyout);
			(void)fflush(ttyout);
		}
		if (ferror(din)) {
			if (errno != EPIPE)
				warn("netin");
			bytes = -1;
		}
		if (ferror(fout))
			warn("local: %s", local);
		break;
	}
	progressmeter(1);
	progress = oprogress;
	preserve = opreserve;
	if (closefunc != NULL)
		(*closefunc)(fout);
	(void)signal(SIGINT, oldintr);
	(void)signal(SIGINFO, oldinti);
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	(void)fclose(din);
	(void)getreply(0);
	if (bytes >= 0 && is_retr) {
		if (bytes > 0)
			ptransfer(0);
		if (preserve && (closefunc == fclose)) {
			mtime = remotemodtime(remote, 0);
			if (mtime != -1) {
				struct utimbuf ut;

				ut.actime = time(NULL);
				ut.modtime = mtime;
				if (utime(local, &ut) == -1)
					fprintf(ttyout,
				"Can't change modification time on %s to %s",
					    local, asctime(localtime(&mtime)));
			}
		}
	}
	return;

abort:

/* abort using RFC959 recommended IP,SYNC sequence */

	progress = oprogress;
	preserve = opreserve;
	if (oldintp)
		(void)signal(SIGPIPE, oldintp);
	(void)signal(SIGINT, SIG_IGN);
	if (!cpend) {
		code = -1;
		(void)signal(SIGINT, oldintr);
		(void)signal(SIGINFO, oldinti);
		return;
	}

	abort_remote(din);
	code = -1;
	if (data >= 0) {
		(void)close(data);
		data = -1;
	}
	if (closefunc != NULL && fout != NULL)
		(*closefunc)(fout);
	if (din)
		(void)fclose(din);
	if (bytes > 0)
		ptransfer(0);
	(void)signal(SIGINT, oldintr);
	(void)signal(SIGINFO, oldinti);
}

/*
 * Need to start a listen on the data channel before we send the command,
 * otherwise the server's connect may fail.
 */
int
initconn()
{
	char *p, *a;
	int result, len, tmpno = 0;
	int on = 1;
	int a0, a1, a2, a3, p0, p1;

reinit:
	if (passivemode) {
		data = socket(AF_INET, SOCK_STREAM, 0);
		if (data < 0) {
			warn("socket");
			return (1);
		}
		if ((options & SO_DEBUG) &&
		    setsockopt(data, SOL_SOCKET, SO_DEBUG, (char *)&on,
			       sizeof(on)) < 0)
			warn("setsockopt (ignored)");
		if (command("PASV") != COMPLETE) {
			if (activefallback) {
				(void)close(data);
				data = -1;
				passivemode = 0;
				activefallback = 0;
				goto reinit;
			}
			fputs("Passive mode refused.\n", ttyout);
			goto bad;
		}

		/*
		 * What we've got at this point is a string of comma
		 * separated one-byte unsigned integer values.
		 * The first four are the an IP address. The fifth is
		 * the MSB of the port number, the sixth is the LSB.
		 * From that we'll prepare a sockaddr_in.
		 */

		if (sscanf(pasv, "%d,%d,%d,%d,%d,%d",
			   &a0, &a1, &a2, &a3, &p0, &p1) != 6) {
			fputs(
"Passive mode address scan failure. Shouldn't happen!\n", ttyout);
			goto bad;
		}

		memset(&data_addr, 0, sizeof(data_addr));
		data_addr.sin_family = AF_INET;
		a = (char *)&data_addr.sin_addr.s_addr;
		a[0] = a0 & 0xff;
		a[1] = a1 & 0xff;
		a[2] = a2 & 0xff;
		a[3] = a3 & 0xff;
		p = (char *)&data_addr.sin_port;
		p[0] = p0 & 0xff;
		p[1] = p1 & 0xff;

		while (connect(data, (struct sockaddr *)&data_addr,
			    sizeof(data_addr)) < 0) {
			if (errno == EINTR)
				continue;
			warn("connect");
			goto bad;
		}
#ifdef IP_TOS
		on = IPTOS_THROUGHPUT;
		if (setsockopt(data, IPPROTO_IP, IP_TOS, (char *)&on,
			       sizeof(int)) < 0)
			warn("setsockopt TOS (ignored)");
#endif
		return (0);
	}

noport:
	data_addr = myctladdr;
	if (sendport)
		data_addr.sin_port = 0;	/* let system pick one */
	if (data != -1)
		(void)close(data);
	data = socket(AF_INET, SOCK_STREAM, 0);
	if (data < 0) {
		warn("socket");
		if (tmpno)
			sendport = 1;
		return (1);
	}
	if (!sendport)
		if (setsockopt(data, SOL_SOCKET, SO_REUSEADDR, (char *)&on,
				sizeof(on)) < 0) {
			warn("setsockopt (reuse address)");
			goto bad;
		}
	if (bind(data, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0) {
		warn("bind");
		goto bad;
	}
	if (options & SO_DEBUG &&
	    setsockopt(data, SOL_SOCKET, SO_DEBUG, (char *)&on,
			sizeof(on)) < 0)
		warn("setsockopt (ignored)");
	len = sizeof(data_addr);
	if (getsockname(data, (struct sockaddr *)&data_addr, &len) < 0) {
		warn("getsockname");
		goto bad;
	}
	if (listen(data, 1) < 0)
		warn("listen");
	if (sendport) {
		a = (char *)&data_addr.sin_addr;
		p = (char *)&data_addr.sin_port;
#define	UC(b)	(((int)b)&0xff)
		result =
		    command("PORT %d,%d,%d,%d,%d,%d",
		      UC(a[0]), UC(a[1]), UC(a[2]), UC(a[3]),
		      UC(p[0]), UC(p[1]));
		if (result == ERROR && sendport == -1) {
			sendport = 0;
			tmpno = 1;
			goto noport;
		}
		return (result != COMPLETE);
	}
	if (tmpno)
		sendport = 1;
#ifdef IP_TOS
	on = IPTOS_THROUGHPUT;
	if (setsockopt(data, IPPROTO_IP, IP_TOS, (char *)&on, sizeof(int)) < 0)
		warn("setsockopt TOS (ignored)");
#endif
	return (0);
bad:
	(void)close(data), data = -1;
	if (tmpno)
		sendport = 1;
	return (1);
}

FILE *
dataconn(lmode)
	const char *lmode;
{
	struct sockaddr_in from;
	int s, fromlen, tos;

	fromlen = sizeof(from);

	if (passivemode)
		return (fdopen(data, lmode));

	s = accept(data, (struct sockaddr *) &from, &fromlen);
	if (s < 0) {
		warn("accept");
		(void)close(data), data = -1;
		return (NULL);
	}
	(void)close(data);
	data = s;
#ifdef IP_TOS
	tos = IPTOS_THROUGHPUT;
	if (setsockopt(s, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int)) < 0)
		warn("setsockopt TOS (ignored)");
#endif
	return (fdopen(data, lmode));
}

void
psummary(notused)
	int notused;
{
	int save_errno = errno;

	if (bytes > 0)
		ptransfer(1);
	errno = save_errno;
}

void
psabort(notused)
	int notused;
{

	alarmtimer(0);
	abrtflag++;
}

void
pswitch(flag)
	int flag;
{
	sig_t oldintr;
	static struct comvars {
		int connect;
		char name[MAXHOSTNAMELEN];
		struct sockaddr_in mctl;
		struct sockaddr_in hctl;
		FILE *in;
		FILE *out;
		int tpe;
		int curtpe;
		int cpnd;
		int sunqe;
		int runqe;
		int mcse;
		int ntflg;
		char nti[17];
		char nto[17];
		int mapflg;
		char mi[MAXPATHLEN];
		char mo[MAXPATHLEN];
	} proxstruct, tmpstruct;
	struct comvars *ip, *op;

	abrtflag = 0;
	oldintr = signal(SIGINT, psabort);
	if (flag) {
		if (proxy)
			return;
		ip = &tmpstruct;
		op = &proxstruct;
		proxy++;
	} else {
		if (!proxy)
			return;
		ip = &proxstruct;
		op = &tmpstruct;
		proxy = 0;
	}
	ip->connect = connected;
	connected = op->connect;
	if (hostname) {
		(void)strncpy(ip->name, hostname, sizeof(ip->name) - 1);
		ip->name[sizeof(ip->name) - 1] = '\0';
	} else
		ip->name[0] = '\0';
	hostname = op->name;
	ip->hctl = hisctladdr;
	hisctladdr = op->hctl;
	ip->mctl = myctladdr;
	myctladdr = op->mctl;
	ip->in = cin;
	cin = op->in;
	ip->out = cout;
	cout = op->out;
	ip->tpe = type;
	type = op->tpe;
	ip->curtpe = curtype;
	curtype = op->curtpe;
	ip->cpnd = cpend;
	cpend = op->cpnd;
	ip->sunqe = sunique;
	sunique = op->sunqe;
	ip->runqe = runique;
	runique = op->runqe;
	ip->mcse = mcase;
	mcase = op->mcse;
	ip->ntflg = ntflag;
	ntflag = op->ntflg;
	(void)strncpy(ip->nti, ntin, sizeof(ip->nti) - 1);
	(ip->nti)[sizeof(ip->nti) - 1] = '\0';
	(void)strcpy(ntin, op->nti);
	(void)strncpy(ip->nto, ntout, sizeof(ip->nto) - 1);
	(ip->nto)[sizeof(ip->nto) - 1] = '\0';
	(void)strcpy(ntout, op->nto);
	ip->mapflg = mapflag;
	mapflag = op->mapflg;
	(void)strncpy(ip->mi, mapin, sizeof(ip->mi) - 1);
	(ip->mi)[sizeof(ip->mi) - 1] = '\0';
	(void)strcpy(mapin, op->mi);
	(void)strncpy(ip->mo, mapout, sizeof(ip->mo) - 1);
	(ip->mo)[sizeof(ip->mo) - 1] = '\0';
	(void)strcpy(mapout, op->mo);
	(void)signal(SIGINT, oldintr);
	if (abrtflag) {
		abrtflag = 0;
		(*oldintr)(SIGINT);
	}
}

void
abortpt(notused)
	int notused;
{

	alarmtimer(0);
	putc('\n', ttyout);
	(void)fflush(ttyout);
	ptabflg++;
	mflag = 0;
	abrtflag = 0;
	longjmp(ptabort, 1);
}

void
proxtrans(cmd, local, remote)
	const char *cmd, *local, *remote;
{
	sig_t oldintr;
	int prox_type, nfnd;
	volatile int secndflag;
	char *cmd2;
	fd_set mask;

#ifdef __GNUC__				/* XXX: to shut up gcc warnings */
	(void)&oldintr;
	(void)&cmd2;
#endif

	oldintr = NULL;
	secndflag = 0;
	if (strcmp(cmd, "RETR"))
		cmd2 = "RETR";
	else
		cmd2 = runique ? "STOU" : "STOR";
	if ((prox_type = type) == 0) {
		if (unix_server && unix_proxy)
			prox_type = TYPE_I;
		else
			prox_type = TYPE_A;
	}
	if (curtype != prox_type)
		changetype(prox_type, 1);
	if (command("PASV") != COMPLETE) {
		fputs("proxy server does not support third party transfers.\n",
		    ttyout);
		return;
	}
	pswitch(0);
	if (!connected) {
		fputs("No primary connection.\n", ttyout);
		pswitch(1);
		code = -1;
		return;
	}
	if (curtype != prox_type)
		changetype(prox_type, 1);
	if (command("PORT %s", pasv) != COMPLETE) {
		pswitch(1);
		return;
	}
	if (setjmp(ptabort))
		goto abort;
	oldintr = signal(SIGINT, abortpt);
	if (command("%s %s", cmd, remote) != PRELIM) {
		(void)signal(SIGINT, oldintr);
		pswitch(1);
		return;
	}
	sleep(2);
	pswitch(1);
	secndflag++;
	if (command("%s %s", cmd2, local) != PRELIM)
		goto abort;
	ptflag++;
	(void)getreply(0);
	pswitch(0);
	(void)getreply(0);
	(void)signal(SIGINT, oldintr);
	pswitch(1);
	ptflag = 0;
	fprintf(ttyout, "local: %s remote: %s\n", local, remote);
	return;
abort:
	(void)signal(SIGINT, SIG_IGN);
	ptflag = 0;
	if (strcmp(cmd, "RETR") && !proxy)
		pswitch(1);
	else if (!strcmp(cmd, "RETR") && proxy)
		pswitch(0);
	if (!cpend && !secndflag) {  /* only here if cmd = "STOR" (proxy=1) */
		if (command("%s %s", cmd2, local) != PRELIM) {
			pswitch(0);
			if (cpend)
				abort_remote(NULL);
		}
		pswitch(1);
		if (ptabflg)
			code = -1;
		(void)signal(SIGINT, oldintr);
		return;
	}
	if (cpend)
		abort_remote(NULL);
	pswitch(!proxy);
	if (!cpend && !secndflag) {  /* only if cmd = "RETR" (proxy=1) */
		if (command("%s %s", cmd2, local) != PRELIM) {
			pswitch(0);
			if (cpend)
				abort_remote(NULL);
			pswitch(1);
			if (ptabflg)
				code = -1;
			(void)signal(SIGINT, oldintr);
			return;
		}
	}
	if (cpend)
		abort_remote(NULL);
	pswitch(!proxy);
	if (cpend) {
		FD_ZERO(&mask);
		FD_SET(fileno(cin), &mask);
		if ((nfnd = empty(&mask, 10)) <= 0) {
			if (nfnd < 0) {
				warn("abort");
			}
			if (ptabflg)
				code = -1;
			lostpeer();
		}
		(void)getreply(0);
		(void)getreply(0);
	}
	if (proxy)
		pswitch(0);
	pswitch(1);
	if (ptabflg)
		code = -1;
	(void)signal(SIGINT, oldintr);
}

void
reset(argc, argv)
	int argc;
	char *argv[];
{
	fd_set mask;
	int nfnd = 1;

	FD_ZERO(&mask);
	while (nfnd > 0) {
		FD_SET(fileno(cin), &mask);
		if ((nfnd = empty(&mask, 0)) < 0) {
			warn("reset");
			code = -1;
			lostpeer();
		}
		else if (nfnd) {
			(void)getreply(0);
		}
	}
}

char *
gunique(local)
	const char *local;
{
	static char new[MAXPATHLEN];
	char *cp = strrchr(local, '/');
	int d, count=0;
	char ext = '1';

	if (cp)
		*cp = '\0';
	d = access(cp == local ? "/" : cp ? local : ".", W_OK);
	if (cp)
		*cp = '/';
	if (d < 0) {
		warn("local: %s", local);
		return ((char *) 0);
	}
	(void)strcpy(new, local);
	cp = new + strlen(new);
	*cp++ = '.';
	while (!d) {
		if (++count == 100) {
			fputs("runique: can't find unique file name.\n", ttyout);
			return ((char *) 0);
		}
		*cp++ = ext;
		*cp = '\0';
		if (ext == '9')
			ext = '0';
		else
			ext++;
		if ((d = access(new, F_OK)) < 0)
			break;
		if (ext != '0')
			cp--;
		else if (*(cp - 2) == '.')
			*(cp - 1) = '1';
		else {
			*(cp - 2) = *(cp - 2) + 1;
			cp--;
		}
	}
	return (new);
}

void
abort_remote(din)
	FILE *din;
{
	char buf[BUFSIZ];
	int nfnd;
	fd_set mask;

	if (cout == NULL) {
		warnx("Lost control connection for abort.");
		if (ptabflg)
			code = -1;
		lostpeer();
		return;
	}

	/*
	 * send IAC in urgent mode instead of DM because 4.3BSD places oob mark
	 * after urgent byte rather than before as is protocol now
	 */
	sprintf(buf, "%c%c%c", IAC, IP, IAC);
	if (send(fileno(cout), buf, 3, MSG_OOB) != 3)
		warn("abort");
	fprintf(cout, "%cABOR\r\n", DM);
	(void)fflush(cout);
	FD_ZERO(&mask);
	FD_SET(fileno(cin), &mask);
	if (din) {
		FD_SET(fileno(din), &mask);
	}
	if ((nfnd = empty(&mask, 10)) <= 0) {
		if (nfnd < 0) {
			warn("abort");
		}
		if (ptabflg)
			code = -1;
		lostpeer();
	}
	if (din && FD_ISSET(fileno(din), &mask)) {
		while (read(fileno(din), buf, BUFSIZ) > 0)
			/* LOOP */;
	}
	if (getreply(0) == ERROR && code == 552) {
		/* 552 needed for nic style abort */
		(void)getreply(0);
	}
	(void)getreply(0);
}
