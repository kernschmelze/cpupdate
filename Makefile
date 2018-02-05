# $FreeBSD: stable/11/usr.sbin/cpucontrol/Makefile 308760 2016-11-17 15:16:52Z avg $

PROG=	cpupdate
MAN=	cpupdate.8
SRCS=	cpupdate.c intel.c 

NO_WCAST_ALIGN=

.include <bsd.prog.mk>
