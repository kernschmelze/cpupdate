/*-Copyright (c) 2018 Stefan Blachmann <sblachmann at gmail.com>
 * Using parts of intel.h, which is...
 * Copyright (c) 2006, 2008 Stanislav Sedov <stas@FreeBSD.org>.
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
 * $FreeBSD: stable/11/usr.sbin/cpucontrol/intel.h 181430 2008-08-08 16:26:53Z stas $
 */

#ifndef INTEL_H
#define	INTEL_H

int  intel_probe(                void);
int  intel_getProcessorInfo(     void *cpuinfo_p, int core);
void intel_printProcessorInfo(   void *cpuinfo_p, int core);
int  intel_updateProcessor(      void *cpuinfo, int core, char *path);
void intel_printUpdateFileStats( char *path);
int  intel_verifyUpdfInteg(      char *path, char *fnamp);


cpu_probeProcessor_t        intel_probe;
cpu_getProcessorInfo_t      intel_getProcessorInfo;
cpu_printProcessorInfo_t    intel_printProcessorInfo;
cpu_printUpdFStats_t        intel_printUpdateFileStats;
cpu_updateProcessor_t       intel_updateProcessor;
cpu_verifyUpdfInteg_t       intel_verifyUpdfInteg;

/* Please see the programmer manual Vol. 3A, page 9-28 
 * for information on the update file format.
 * Document available at URL: XXX
 */
typedef struct {
	uint32_t	header_version;		/* Version of the header. */
	int32_t		revision;		/* Unique version number. */
	uint32_t	date;			/* Date of creation in BCD. */
	uint32_t	cpu_signature;		/* Extended family, extended
						   model, type, family, model
						   and stepping. */
	uint32_t	checksum;		/* Sum of all DWORDS should
						   be 0. */
	uint32_t	loader_revision;	/* Version of the loader
						   required to load update. */
	uint32_t	cpu_flags;		/* Platform IDs encoded in
						   the lower 8 bits. */
  uint32_t  data_size;
	uint32_t	total_size;
	uint8_t		reserved[12];
} intel_fw_header_t;

typedef struct {
	uint32_t	cpu_signature;
	uint32_t	cpu_flags;
	uint32_t	checksum;
} intel_cpu_signature_t;

/* bitfield  structures are compiler specific, similar to little/big endian */
/* TODO implement compiler switch */
#if 0
typedef struct {
  unsigned int reserved1 : 4;
  unsigned int ExtendedFamilyID : 8;
  unsigned int ExtendedModelID : 4;
  unsigned int reserved2 : 2;
  unsigned int ProcessorType : 2;
  unsigned int FamilyID : 4;
  unsigned int Model : 4;
  unsigned int SteppingID : 4;
} cpuinfoBitF;
#else
typedef struct {
  unsigned int SteppingID : 4;
  unsigned int Model : 4;
  unsigned int FamilyID : 4;
  unsigned int ProcessorType : 2;
  unsigned int reserved2 : 2;
  unsigned int ExtendedModelID : 4;
  unsigned int ExtendedFamilyID : 8;
  unsigned int reserved1 : 4;
} cpuinfoBitF;
#endif

typedef union {
  uint32_t     signatInt;
  cpuinfoBitF  signatBitF;
} intel_SignatUnion;

typedef struct {
  uint32_t    sig,
              cpu_flags,
              checksum;
} intel_ExtSignat_t;

typedef union {
  intel_ExtSignat_t    sigS;
  intel_SignatUnion    sigU;
} intel_ExtSignatUnion;


typedef struct {
  intel_ExtSignatUnion esig;
  uint32_t             family, 
                       model,
                       numCoresPerProc,
                       numProcessors;
  int32_t              ucoderev;
  uint32_t             flags;
} intel_ProcessorInfo;

typedef struct {
	uint32_t	sig_count;
	uint32_t	checksum;
	uint8_t		reserved[12];
} intel_ext_header_t;

typedef struct {
  uint8_t  *image;
  uint32_t  data_size;
  uint32_t  payload_size;
  uint32_t  ext_size;
  uint32_t  total_size;
  uint8_t   has_ext_table;
  intel_ext_header_t    *ext_header;  
  intel_ExtSignatUnion  *ext_table;   /* actually pointer to array of ext headers */ 
  int       ext_table_size;
} intel_hdrhdr_t;
  
typedef struct {
  int  headerindex;   /* apparently not used yet by Intel */
  int  blobindex;
  uint32_t bestrev;
} intel_flagmatch;


#define	INTEL_HEADER_VERSION	0x00000001
#define	INTEL_LOADER_REVISION	0x00000001

#endif /* !INTEL_H */
