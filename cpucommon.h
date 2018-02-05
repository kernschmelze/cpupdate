/*-Copyright (c) 2018 Stefan Blachmann <sblachmann at gmail.com>
 * Using parts of cpucontrol.h, which is...
 * Copyright (c) 2008 Stanislav Sedov <stas@FreeBSD.org>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: stable/11/usr.sbin/cpucontrol/cpucontrol.h 181430 2008-08-08 16:26:53Z stas $
 */

#ifndef CPUCOMMON_H
#define	CPUCOMMON_H

typedef int  cpu_probeProcessor_t(     void);
typedef int  cpu_getProcessorInfo_t(   void *, int);
typedef void cpu_printProcessorInfo_t( void *, int);
typedef void cpu_printUpdFStats_t(     char *);
typedef int  cpu_updateProcessor_t(    void *, int, char *);
typedef int  cpu_verifyUpdfInteg_t(    char *, char *);
/*
typedef void cpu_update_t(   const char *dev, const char *image);
*/
extern int  verbosity;
extern int  writeit;
extern int  outputmode;
extern int  vendormode;
extern int  extractbuggedfiles;
extern char extractdir[ MAXPATHLEN];

/* OUTPMODE_ constants control what is being output, depending on function used */
#define OUTPMODE_I    1
#define OUTPMODE_F    2
#define OUTPMODE_U    4
#define OUTPMODE_C    8

#define INFO(level, ...) if ((level) <= verbosity) printf(__VA_ARGS__); 

#define MAXVENDORNAMELEN 100
#define MAXCORES 257
#define MAXHEADERS 8

/* 8 chars for yyyy/mm/dd + \0 */
#define DATELEN 11
#endif /* !CPUCOMMON_H */
