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

#ifndef CPUPDATE_H
#define	CPUPDATE_H

typedef union {
  intel_ProcessorInfo intel;
} cpuinfobuf_t;

typedef struct {
  const char                vendorName[ MAXVENDORNAMELEN];
  cpu_probeProcessor_t     *probe;   
  cpu_getProcessorInfo_t   *getInfo;
  cpu_printProcessorInfo_t *printInfo;
  cpu_printUpdFStats_t     *printUpdFStats;
  cpu_updateProcessor_t    *update;
  cpu_verifyUpdfInteg_t    *verifyUpdF;
} cpu_handler_t;

#define NHANDLERS (sizeof(handlers) / sizeof(*handlers))

#endif /* !CPUPDATE_H */
