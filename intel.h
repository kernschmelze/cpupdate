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
 */

#ifndef INTEL_H
#define	INTEL_H

/* Please see the programmer manual Vol. 3A, page 9-28 
 * for information on cpu identification and the microcode update file format.
 * Document available at URL: https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-3a-part-1-manual.pdf
 * Note: The multi-blobbed format isn't described there, but it is simple.
 * Just all blob files for a particular family-model-stepping concatenated.
 */

/* The header, all values only regard the blob of the particular header. */
struct intel_uc_header_t {
				/* Version of the header. */
	uint32_t	header_version;		
				/* Unique version number. */
	int32_t		revision;
				/* Date of creation in BCD. */
	uint32_t	date;
				/* signature: family, model, type and stepping. */
	uint32_t	cpu_signature;
				/* Checksum: Sum of all DWORDS should be 0. */
	uint32_t	checksum;
				/* Version of the loaderrequired to load update. */
	uint32_t	loader_revision;
				/* Platform IDs encoded in the lower 8 bits. */
	uint32_t	cpu_flags;
				/* payload data size: If 0, datasize = 2000 bytes, else the data_size amount */
    uint32_t	data_size;
				/* total blob size, including header */
	uint32_t	total_size;
	uint8_t		reserved[12];
};

/* The signature data structure returned by the CPUID function */
struct intel_cpu_signature_t {
	uint32_t	cpu_signature;
	uint32_t	cpu_flags;
	uint32_t	checksum;
};

/* order of bitfield structure members is compiler specific */
/* TODO add compiler switch */
#if 0
struct cpuinfoBitF {
	unsigned int reserved1 			: 4;
	unsigned int ExtendedFamilyID 	: 8;
	unsigned int ExtendedModelID 	: 4;
	unsigned int reserved2 			: 2;
	unsigned int ProcessorType 		: 2;
	unsigned int FamilyID 			: 4;
	unsigned int Model 				: 4;
	unsigned int SteppingID 		: 4;
};
#else
// clang
struct cpuinfoBitF {
	unsigned int SteppingID 		: 4;
	unsigned int Model 				: 4;
	unsigned int FamilyID 			: 4;
	unsigned int ProcessorType 		: 2;
	unsigned int reserved2 			: 2;
	unsigned int ExtendedModelID	: 4;
	unsigned int ExtendedFamilyID	: 8;
	unsigned int reserved1 			: 4;
};
#endif

union intel_SignatUnion {
	uint32_t     		sigInt;
	struct cpuinfoBitF	sigBitF;
};

// extended signature, is only subset of full 1st signature
struct intel_ExtSignat_t {
	uint32_t	sig,
				cpu_flags,
				checksum;
};

union intel_ExtSignatUnion {
	struct intel_ExtSignat_t	sigS;
	union  intel_SignatUnion	sigU;
};

struct intel_ProcessorInfo {
	union intel_SignatUnion
				sig;
	int32_t 	ucoderev;
	uint32_t	flags;
};


struct intel_ext_header_t {
	uint32_t	sig_count;
	uint32_t	checksum;
	uint8_t		reserved[12];
};


struct intel_hdrhdr_t {
	uint8_t	   *image;
	uint32_t	data_size;
	uint32_t	payload_size;
	uint32_t	ext_size;
	uint32_t	total_size;
	uint8_t 	has_ext_table;
	struct intel_ext_header_t
			   *ext_header;
	union intel_ExtSignatUnion
			   *ext_table;		/* actually pointer to array of ext headers */ 
	int			ext_table_size;
};


struct intel_flagmatch {
	int			headerindex;
	int			blobindex;
	uint32_t 	bestrev;
};


struct intel_ucinfo {
	// image of whole file, containing all blobs
	void   *image;
	// whole image size in bytes
	int 	imagesize;
	int		blobcount;
	struct intel_hdrhdr_t
			hdrhdrs[ MAXHEADERS];
};


extern struct vendor_funcs intel_funcs;

// define the indents for formatting the microcode file info stuff
#define INDENT_0 ("  ")
#define INDENT_1 ("    ")
#define INDENT_2 ("      ")


#endif /* !INTEL_H */
