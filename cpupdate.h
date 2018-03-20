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
 */

#ifndef CPUPDATE_H
#define	CPUPDATE_H
#define CPUPDATE_VERSION ("1.0.0")

// parameter structure with vender-unspecific parameters
struct cpupdate_params {
	// pointer to vendor-specific cpusinfo structs array, set to NULL if not there/not inited
	void   *coreinfop;
	// pointer to vendor-specific ucode file struct, set to NULL if not there/not inited
	void   *ucodeinfop;
	// set by commandline. used for loadcheckmicrocodefile, else use file_data::fname created by vendor_probe
	char 	filepath[  MAXPATHLEN];
	// used for loadcheckmicrocodefile, primary path (user supplied microcodes from vendor library)
	char 	primdir[   MAXPATHLEN];
	// used for primary path (OS supplied microcodes from platomav collection)
	char 	secdir[    MAXPATHLEN];
	// used for generate, checkstats
	char 	srcdir[    MAXPATHLEN];
	char 	targetdir[ MAXPATHLEN]; // used for  generate
	int		writeit;				// bool flag: if nonzero, do actual uploading and not simulate
};

typedef int (*hnd_f)( struct cpupdate_params *);
typedef const char * (*hnd_n)( void);

struct vendor_funcs {
	hnd_f	probe,					// gets cpus stats, inits data structure if matches
			printcpustats,			// prints processor(s) stats. probe must have been done before
			loadcheckmicrocode,		// loads and verifies microcode file. File to be used: a) params->filepath, b) params->prim/secdir + cpusinfo->ffmmssname
			printmicrocodestats,	// prints microcode files' stats. loadcheckmicrocode must have been done before
			update,					// updates processor(s). probe and loadcheckmicrocode must have been done before
			freeucodeinfo,			// frees ucode info (for loading another microcode file)
			extractformat,			// extract multi-blobbed files to single blobs
			compactformat;			// convert/compact single blobs to multi-blobbed files
	hnd_n	getvendorname;			// return VENDORNAME string (see macros below)
};

// the vendor names are also used as directory paths for microcode subdirectories
#define VENDORNAME_INTEL ("Intel")
#define VENDORNAME_AMD ("AMD")
#define VENDORNAME_VIA ("VIA")
#define MICROCODE_REPO_PATH_PRIM ("/usr/local/share/cpupdate/CPUMicrocodes/primary")
#define MICROCODE_REPO_PATH_SEC ("/usr/local/share/cpupdate/CPUMicrocodes/secondary")

extern int  vendormode;
extern int	verbosity;				// spamminess level
extern int	numCores;				// number of present cores
extern char *pgmn;			// program name for messages

#define INFO(level, ...) if ((level) <= verbosity) printf(__VA_ARGS__); 
#define NHANDLERS (sizeof(handlers) / sizeof(*handlers))

#define MAXVENDORNAMELEN 100
#define MAXCORES 257
#define MAXHEADERS 8
/* 8 chars for yyyy/mm/dd + \0 */
#define DATELEN 11

#endif /* !CPUPDATE_H */
