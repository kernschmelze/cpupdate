/*-Copyright (c) 2018 Stefan Blachmann <sblachmann at gmail.com>
 * Using parts of intel.c, which is...
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/ioccom.h>
#include <sys/cpuctl.h>

#include <machine/cpufunc.h>
#include <machine/specialreg.h>
#define INTEL_C

#include "cpupdate.h"
#include "intel.h"

int intel_probe( struct cpupdate_params *);
int intel_loadcheckmicrocode( struct cpupdate_params *);
int intel_printcpustats( struct cpupdate_params *);
int intel_printmicrocodestats( struct cpupdate_params *);
int intel_update( struct cpupdate_params *);
int intel_freeucodeinfo( struct cpupdate_params *params);
int intel_extractformat( struct cpupdate_params *params);
int intel_compactformat( struct cpupdate_params *params);
const char *intel_getvendorname( struct cpupdate_params *);

struct vendor_funcs intel_funcs = {
	(hnd_f)	&intel_probe,
	(hnd_f)	&intel_printcpustats,
	(hnd_f)	&intel_loadcheckmicrocode,
	(hnd_f)	&intel_printmicrocodestats,
	(hnd_f)	&intel_update,
	(hnd_f)	&intel_freeucodeinfo,
	(hnd_f)	&intel_extractformat,
	(hnd_f)	&intel_compactformat,
	(hnd_n)	&intel_getvendorname
};

static uint32_t intel_getFamily( uint32_t *sig);
static uint32_t intel_getModel( uint32_t *sig);
static int intel_getCoreInfo( struct intel_ProcessorInfo *coreinfo, int core);
static int intel_getCoresInfo( struct intel_ProcessorInfo *coreinfos);
static void printcpustats( struct intel_ProcessorInfo *info, int s, int e);
static int readucfile( void *ucodeinfop, char *upfilepath);
static int intel_getHdrInfo( struct intel_hdrhdr_t *hdr, const char *filename);
static char *getdatestr( uint32_t datefield);
static void intel_printSignatInfo( uint32_t *sig_p, const char *ind);
static void intel_printExtSignatInfo( void *sig_p, const char *ind);
static void intel_printHeadersInfo( struct intel_hdrhdr_t *hdrhdr);


/* From https://software.intel.com/en-us/articles/intel-architecture-and-processor-identification-with-cpuid-model-and-family-numbers
 * The Family number is an 8-bit number derived from the processor 
 * signature by adding the Extended Family number (bits 27:20) and 
 * the Family number (bits 11:8). 
 */
static uint32_t
intel_getFamily( uint32_t *sig)
{
  union intel_SignatUnion *sigp = (union intel_SignatUnion *) sig;
  return sigp->sigBitF.ExtendedFamilyID + sigp->sigBitF.FamilyID;
}


/* The Model number is an 8 bit number derived from the processor
 * signature by shifting the Extended Model number (bits 19:16)
 * 4 bits to the left and adding the Model number (bits 7:4) . 
 * See section 5.1.2.2 of the "Intel Processor Identification and
 * the CPUID Instruction".
 */
static uint32_t
intel_getModel( uint32_t *sig)
{
  union intel_SignatUnion *sigp = (union intel_SignatUnion *) sig;
  return (sigp->sigBitF.ExtendedModelID << 4) + sigp->sigBitF.Model;
}


static int
intel_getCoreInfo( struct intel_ProcessorInfo *coreinfo, int core)
{
	int					r, cpufd = -1;
	char 				cpudev[ MAXPATHLEN];
	cpuctl_msr_args_t   msrargs;
	cpuctl_cpuid_args_t idargs = {
		.level  = 1,  /* Signature. */
	};

	sprintf( cpudev, "/dev/cpuctl%d", core);
	if ((r = ((cpufd = open( cpudev, O_RDWR)) < 0))) {
		INFO( 0, "could not open %s for writing\n", cpudev);
	}
	if (!r) {
		/* Read Platform ID, see Intel Manual Vol. 3A, section 9.11.04, pg 9-32+33 */
		msrargs.msr = MSR_IA32_PLATFORM_ID;
		if ((r = ioctl( cpufd, CPUCTL_RDMSR, &msrargs)) < 0) {
			INFO( 0, "Reading platform ID for %s failed\n", cpudev);
		} else {
			r = 0;
			/* MSR_IA32_PLATFORM_ID contains flag in BCD in bits 52-50. */
			coreinfo->flags = 1 << ((msrargs.data >> 50) & 7);
		}
	}
	if (!r) {
		/* preinit MSR - to make sure that cpuid actually returns correct values 
		 * see also PR 192487 and Intel Manual Vol. 3A, pg 9-36+37
		 */
		msrargs.msr = MSR_BIOS_SIGN;
		msrargs.data = 0;
		if ((r = ioctl( cpufd, CPUCTL_WRMSR, &msrargs)) < 0) {
			INFO( 0, "Initialization for CPUID for %s failed\n", cpudev);
		} else
			r = 0;
	}
	if (!r && ((r = ioctl( cpufd, CPUCTL_CPUID, &idargs)) < 0) ) {
		INFO( 0, "%s CPUID failed\n", cpudev);
	}
	if (!r) {
		coreinfo->sig.sigInt = idargs.data[0];
// 		coreinfo->esig.sigS.cpu_flags = idargs.data[1];
// 		coreinfo->esig.sigS.checksum  = idargs.data[2];
		if ((r = ioctl( cpufd, CPUCTL_RDMSR, &msrargs)) < 0) {
			INFO( 0, "%s MSR read failed\n", cpudev);
		} else
			r = 0;
	} 
	if (!r) {
		msrargs.msr = MSR_BIOS_SIGN;
		if ((r = ioctl(cpufd, CPUCTL_RDMSR, &msrargs)) < 0) {
			INFO( 0, "%s signature read failed\n", cpudev);
		}
	}
	if (!r) {
		/* Micorocode revision in the high dword. See Intel Manual Vol 3A, pg 9-36. */
		coreinfo->ucoderev = msrargs.data >> 32; 
		INFO( 12, "%s identification successful!\n", cpudev);
	}
	if (cpufd >= 0) close( cpufd);
	return r;
}


static int
intel_getCoresInfo( struct intel_ProcessorInfo *coreinfos)
{
	int			core, r;
	struct intel_ProcessorInfo 
				*coreinfo;
	
	assert( numCores);
	for( core = 0; core < numCores; ++core){
		coreinfo = coreinfos + core;
		r = intel_getCoreInfo( coreinfo, core);
		if (r) 
			break;
	}
	return r;
}


int 
intel_probe( struct cpupdate_params *params)
{

	// stage 1: check whether we have an Intel cpu
	/* See https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf
	 * page 3-204, Figure 3-6
	 */
	char vendor[ MAXVENDORNAMELEN];
	cpuctl_cpuid_args_t idargs = {
		.level  = 0,
	};
	const char *cpudev = "/dev/cpuctl0";
	int cpufd, r;
  
	if ((r = ((cpufd = open( cpudev, O_RDONLY)) < 0))) {
		INFO( 0, "error opening %s for reading\n", cpudev);
		r = -1;
	}
	if (!r && ((r = ((ioctl( cpufd, CPUCTL_CPUID, &idargs) < 0))))) {
		INFO( 0, "ioctl( CPUCTL_CPUID) failed\n");
		r = -1;
	}
	if (!r) {
		((uint32_t *)vendor)[0] = idargs.data[1];
		((uint32_t *)vendor)[1] = idargs.data[3];
		((uint32_t *)vendor)[2] = idargs.data[2];
		vendor[12] = '\0';
		r = (strncmp( vendor, INTEL_VENDOR_ID, sizeof( INTEL_VENDOR_ID))) ? 1 : 0;
	} else
		r = -1;
	if (r)
		return r;
	
	// if we reached here, we have an Intel CPU.
	// First allocate coreinfo structures for each core.
	// Then walk cores and collect their data.

	if ((params->coreinfop = calloc( numCores, sizeof( struct intel_ProcessorInfo))) == NULL) {
		INFO( 0, "Failed to allocate memory for coreinfos structures\n");
		return 1;
	}
	r = intel_getCoresInfo( params->coreinfop);
	return r;
}


static void
printcpustats( struct intel_ProcessorInfo *info, int s, int e)
{
	INFO( 11, "Core %d to %d: Type %01d  FamID %01x  ModID %01x  ExtFam %02x  ExtMod %01x\n", 
				s, 
				e,
				info->sig.sigBitF.ProcessorType,
				info->sig.sigBitF.FamilyID,
				info->sig.sigBitF.Model,
				info->sig.sigBitF.ExtendedFamilyID,
				info->sig.sigBitF.ExtendedModelID);
	INFO( 10, "Core %d to %d: CPUID: %x  Fam %02x  Mod %02x  Step %02x  Flag %02x uCode %08x\n", 
				s, 
				e,
				info->sig.sigInt,
				intel_getFamily( &info->sig.sigInt),
				intel_getModel( &info->sig.sigInt),
				info->sig.sigBitF.SteppingID,
				info->flags,
				info->ucoderev);
}


int
intel_printcpustats( struct cpupdate_params *params)
{
	struct intel_ProcessorInfo *coreinfo = (struct intel_ProcessorInfo *) params->coreinfop;

	// as there might be multiple processors of different ucoderevs etc,
	// walk all cores and determine ranges of equal cores.
	// either when all cores are walked, or a core with different stats 
	// has been found, print out stats
	int startcore = 0;
	int ncore = 1;
	struct intel_ProcessorInfo
			*ncoreinfo,
			*startcoreinfo = coreinfo;
	
	for ( ; ncore < numCores ; ++ncore) {
	 	ncoreinfo = coreinfo + ncore;
    	if (	ncoreinfo->sig.sigBitF.SteppingID		!= startcoreinfo->sig.sigBitF.SteppingID 		||
    			ncoreinfo->sig.sigBitF.Model			!= startcoreinfo->sig.sigBitF.Model 			||
    			ncoreinfo->sig.sigBitF.FamilyID			!= startcoreinfo->sig.sigBitF.FamilyID 			||
    			ncoreinfo->sig.sigBitF.ProcessorType	!= startcoreinfo->sig.sigBitF.ProcessorType		||
    			ncoreinfo->sig.sigBitF.Model			!= startcoreinfo->sig.sigBitF.Model 			||
    			ncoreinfo->sig.sigBitF.ExtendedModelID	!= startcoreinfo->sig.sigBitF.ExtendedModelID	||
    			ncoreinfo->sig.sigBitF.ExtendedFamilyID	!= startcoreinfo->sig.sigBitF.ExtendedFamilyID	||
				ncoreinfo->flags						!= startcoreinfo->flags							||
				ncoreinfo->ucoderev						!= startcoreinfo->ucoderev) {
	 		// core #ncore is different than core #startcore..#(ncore-1).
			// Print core info for that block
			printcpustats( startcoreinfo, startcore, ncore - 1);
			startcore = ncore;
			startcoreinfo = ncoreinfo;
		}
	}
	// make sure also the last block of identical cores are shown
	if (startcore < ncore)
		printcpustats( startcoreinfo, startcore, ncore - 1);
	return 0;
}


static int
readucfile( void *ucodeinfop, char *upfilepath)
{
	struct intel_ucinfo 
			   *ucinfo;
	int			updfd = 0;
	int			r = 0;
	struct stat	st;

	ucinfo = (struct intel_ucinfo *) ucodeinfop;
	if ((updfd = open( upfilepath, O_RDONLY, 0)) < 0) {
		INFO( 12, "Failed to open %s file\n", upfilepath);
		r = 1;
	}
	if (!r) {  
		if ((r = fstat( updfd, &st))) {
			INFO( 0, "File %s fstat failed\n", upfilepath);
			r = 1;
		}
	}
	if (!r) {  
		if ((ucinfo->image = malloc( st.st_size)) == NULL) {
			INFO( 0, "Buffer allocation of %ld bytes failed\n", st.st_size);
			r = 1;
		}
	}
	if (!r) {  
		if (read( updfd, ucinfo->image, st.st_size) != st.st_size) {
			INFO( 0, "Reading %ld bytes from file %s failed\n", st.st_size, upfilepath );
			free( ucinfo->image);
			r = 1;
		} else 
			ucinfo->imagesize = st.st_size;
	}
	if (updfd >= 0)
		close( updfd);
	return r;
}


/* populates the hdrhdr structure while validating the blob.
 * hdr-> image must be preset to point at the blob start address,
 * this saves us an argument
 */
static int
intel_getHdrInfo( struct intel_hdrhdr_t *hdr, const char *filename)
{
	int r = 0;
	struct intel_uc_header_t *image = (struct intel_uc_header_t *) hdr->image;
	
	/* check if the [first] header looks valid at the first glimpse */
	if (!r) {
		if (image->header_version != 1 || image->loader_revision != 1) {
			INFO( 0, "File %s: Unsupported version\n", filename);
			r = -1;
		}
	}
	if (!r) {
		/* According to spec, if data_size == 0, then size of ucode = 2000. */
		hdr->data_size = (image->data_size == 0) ? 2000 : image->data_size;
		/* spec does not mention that if data size field is zero, total size field is also zero */
		hdr->total_size = (image->data_size == 0 && image->total_size == 0) 
		             ? (hdr->data_size + sizeof( struct intel_uc_header_t)) : image->total_size;
		/* data size must be a multiple number of dwords (4 byte) */
		if (hdr->data_size % sizeof( uint32_t)) {
			INFO( 0, "File %s: Data size is not multiple of dword\n", filename);
			r = -1;
		}
	}
	if (!r) {
		hdr->payload_size = hdr->data_size + sizeof( struct intel_uc_header_t);
	}
	if (!r) {
		uint32_t sum = 0;
		uint32_t *p = (uint32_t *) image;
		int n = hdr->total_size / sizeof( uint32_t);
		for (int i = 0; i < n; i++, p++)
			sum += *p;
		/* checksum (sum) must be zero. image->checksum only serves to get it zero */
		if (sum) {
			INFO( 0, "File %s: Image's primary checksum invalid\n", filename);
			r = -1;
		}
	}
	/* if present, examine extended headers */
	hdr->has_ext_table = 0;   /* default if not changed in the following checks */
	if (!r && (hdr->total_size > hdr->payload_size)) {
		/* does image have an extended signature table? */
		hdr->ext_size = hdr->total_size - hdr->payload_size;
		hdr->has_ext_table = 1;
		INFO( 12, "File %s: Has extended header\n", filename);
		/* extended headers were introduced with family 0FH, model 03H. (see vol 3A, pg 9-28) */
		if ( intel_getFamily( &image->cpu_signature) < 0x0f ||
	  			(intel_getFamily( &image->cpu_signature) == 0x0f && intel_getModel( &image->cpu_signature) < 0x03) ) {
			INFO( 0, "File %s: Extended header present, but officially not supported with that family/model\n", filename);
//			r = -1;
		}
	}
	if (!r && hdr->has_ext_table) {
		if (hdr->ext_size < (signed) sizeof( struct intel_ext_header_t)) {
			INFO( 0, "File %s: Image's extended header incomplete\n", filename);
			r = -1;
		}
	}
	if (!r && hdr->has_ext_table) {
		hdr->ext_header = (struct intel_ext_header_t *) (((uint8_t *) image) + hdr->payload_size);
		hdr->ext_table = (union intel_ExtSignatUnion *) (hdr->ext_header + 1);
		/* Check the extended table size. */
		hdr->ext_table_size = sizeof( struct intel_ext_header_t) + hdr->ext_header->sig_count * sizeof( union intel_ExtSignatUnion);
		if (hdr->ext_table_size + hdr->payload_size > hdr->total_size) {
			INFO( 0, "File %s: Extended signature table incomplete\n", filename);
			r = -1;
		}
	}
#if 0
	if (!r && hdr->has_ext_table) {
		uint32_t sum = 0;
		uint32_t *p = (uint32_t *) hdr->ext_table;
		int n = hdr->ext_table_size / sizeof( uint32_t);
		for ( int i = 0; i < n; i++, p++)
			sum += *p;
		if ((r = sum)) {
			INFO( 0, "File %s: Extended signature table checksum invalid\n", filename);
			r = -1;
		}
	}
	if (!r && hdr->has_ext_table) {
		for (uint32_t en = 0; en < hdr->ext_header->sig_count; ++en) {
			/* do a checksum for the signature like described in vol 3A, pg 9-30, see table field checksum[n].
			 * To explain what's going on in this block, here the quote:
			 *     To calculate the Checksum, substitute the Primary Processor
			 *     Signature entry and the Processor Flags entry with the
			 *     corresponding Extended Patch entry. Delete the Extended Processor
			 *     Signature Table entries. The Checksum is correct when the
			 *     summation of all DWORDs that comprise the created Extended
			 *     Processor Patch results in 00000000H.
			 */
			/* make a copy first */
			uint32_t *cimage = malloc( hdr->total_size);
			assert( cimage != NULL);
			memcpy( cimage, hdr->image, hdr->total_size);
			/* substitute the Primary Processor Signature entry */
			struct intel_uc_header_t *cimagehdr = (struct intel_uc_header_t *) cimage;
			union intel_ExtSignatUnion *extsig = hdr->ext_table + en;
			cimagehdr->cpu_signature = extsig->sigS.sig;
			cimagehdr->cpu_flags = extsig->sigS.cpu_flags;
			/* now verify the checksum of the copied block. */
			uint32_t sum = 0;
			uint32_t *p = cimage;
			int n = hdr->total_size / sizeof( uint32_t);
			for (int i = 0; i < n; i++, p++)
				sum += *p;
			free( cimage);
			if (cimagehdr->checksum != sum) {
				INFO( 0, "File %s: Image's extended blob #%d checksum invalid\n", filename, en);
				r = -1;
			}
		}
	}
#endif
	return r;
}
  

int 
intel_loadcheckmicrocode( struct cpupdate_params *params)
{
	char upfilepath[ MAXPATHLEN];
	char upfilename[ MAXPATHLEN];
	struct intel_ProcessorInfo *info;
	struct intel_ucinfo *ucinfo;
	int r;
	int nodir = 1;				// bool: microcode directory given?
	int gotfile = 0;			// bool: got microcode file?

	if ((params->ucodeinfop = calloc( 1, sizeof( struct intel_ucinfo))) == NULL) {
		INFO( 0, "Could not allocate ucodeinfo struct!\n");
		return 1;
	}
	ucinfo = params->ucodeinfop;
	// if filepath has been preset, use this
	if (strlen( params->filepath)) {
		strcpy( upfilepath, params->filepath);
		r = readucfile( ucinfo, upfilepath);
		if (r) {
			INFO( 0, "File %s: Does not exist or could not be read!\n", upfilepath);
		} else
			gotfile = 1;
	} else {
		// first construct filename from cpu info
		assert( params->coreinfop != NULL);
		info = (struct intel_ProcessorInfo *) params->coreinfop;
		/* construct family-model-stepping filename for microcode binary */
		if (snprintf( upfilename, sizeof( upfilename), "%02x-%02x-%02x",
					intel_getFamily( &info->sig.sigInt),
					intel_getModel( &info->sig.sigInt),
					info->sig.sigBitF.SteppingID) >= sizeof( upfilename)) {
			INFO( 0, "filename buffer for %s too short\n", upfilename);
			exit( 1);
		}
		// then check primary, then secondary ucodes directory for file
		if (strlen( params->primdir)) {
			nodir = 0;
			if (snprintf( upfilepath, sizeof( upfilepath), "%s/%s", params->primdir, 
						upfilename) >= sizeof( upfilepath)) {
				INFO( 0, "filename buffer for primdir %s too short\n", upfilepath);
				exit( 1);
			}
			r = readucfile( ucinfo, upfilepath);
			if (!r) {
				gotfile = 1;
				strcpy( params->filepath, upfilepath);
			}
		}
		if ((!gotfile) && strlen( params->secdir)) {
			nodir = 0;
			if (snprintf( upfilepath, sizeof( upfilepath), "%s/%s", params->secdir, 
					upfilename) >= sizeof( upfilepath)) {
				INFO( 0, "filename buffer for secdir %s too short\n", upfilepath);
				exit( 1);
			}
			r = readucfile( ucinfo, upfilepath);
			if (!r) {
				gotfile = 1;
				strcpy( params->filepath, upfilepath);
			}
		}
		if (nodir) {
			INFO( 0, "No file and no directories specified!\n");
			r = 1;
		} else if (r) {
			INFO( 0, "File %s: Read error!\n", upfilepath);
			r = 1;
		}
	}
	if (!r && gotfile) {
		INFO( 11, "Update file %s has been read.\n", upfilepath);
		// now we have the file, check its validity
		// and build the pointers to its structures
		
		/* get first header to get the file's basic information */  
		ucinfo->hdrhdrs[ 0].image = ucinfo->image;
		if (!r && (r = intel_getHdrInfo( &ucinfo->hdrhdrs[ 0], upfilepath))) {
			INFO( 0, "File %s: Error in [first] header\n", upfilepath);
		}
		
		if (!r && ucinfo->hdrhdrs[ 0].has_ext_table) {
			// extended headers support dropped because Intel seems to have dropped them 
			// in favor of new file format
			// but warn when found, to avoid possible surprises
			INFO( 11, "File %s: Blob %d has extended header!\n - extended header NOT verified and not used by cpupdate!!\n", 
				  upfilepath, ucinfo->blobcount);
		} 

		ucinfo->blobcount = 1;
		if (!r && ((ucinfo->hdrhdrs[ 0]).total_size != ucinfo->imagesize)) {
			// check how many updates the file contains [usually each for different processor flags, up to 8]
			// now we have to walk through all headers like a linked list
			uint32_t tsiz = ucinfo->hdrhdrs[ 0].total_size;

			for ( ; ucinfo->blobcount <= MAXHEADERS; ++ucinfo->blobcount) {
				if ( ucinfo->blobcount > MAXHEADERS) {
					INFO( 0, "File %s: Contains at least %d headers, but only %d are supported!\n", 
								upfilepath, ucinfo->blobcount, MAXHEADERS);
					r = 1;
					break;
				}
				// as the blobs are concatennated, use the hdrhdr.total_size field as pointer offset
				ucinfo->hdrhdrs[ ucinfo->blobcount].image = 
								ucinfo->hdrhdrs[ ucinfo->blobcount - 1].image + 
								ucinfo->hdrhdrs[ ucinfo->blobcount - 1].total_size;
								
				r = intel_getHdrInfo( &ucinfo->hdrhdrs[ ucinfo->blobcount], upfilepath);
				if (r) {
					INFO( 0, "File %s: Header/Blob %d seems to be inconsistent!\n", upfilepath, 
							ucinfo->blobcount);
					break;
				} 
				if (ucinfo->hdrhdrs[ ucinfo->blobcount].has_ext_table) {
					// extended headers support dropped because Intel seems to have dropped them 
					// in favor of new file format
					// but warn when found, to avoid possible surprises
					INFO( 11, "File %s: Blob %d has extended header - extended header NOT checked!!\n", 
						  upfilepath, ucinfo->blobcount);
				} 
				// we are finished when the blob's end is at EOF
				tsiz += ucinfo->hdrhdrs[ ucinfo->blobcount].total_size;
				if (ucinfo->imagesize == tsiz) {
					break;
				} else if (ucinfo->imagesize < tsiz) {
					INFO( 0, "File %s: Blob %d goes past EOF!\n", upfilepath, ucinfo->blobcount);
					r = 1;
					break;
				}
			}
		}
		if (!r && ucinfo->blobcount > 1) {
			INFO( 12, "File %s contains %d update blobs\n", upfilepath, ucinfo->blobcount);
		} else {
			INFO( 12, "File %s is single-blobbed\n", upfilepath);
		}
		/* verify that there is no conflicting/ambiguous situation that makes matching correct update impossible
		 *    -headers should all have same cpuid but different flags
		 *    -if there are overlapping flags, the revision ids must be different to remove ambiguity TODO XXX
		 */
		if (!r && ucinfo->blobcount > 1) {
			/* only if multiple blobs present: check the ones beyond the beginning one */
			uint32_t cpu_flags_hit = ((struct intel_uc_header_t *) (ucinfo->hdrhdrs[ 0].image))->cpu_flags;
			union intel_SignatUnion *signat0 = (union intel_SignatUnion *) 
							&((struct intel_uc_header_t *) (ucinfo->hdrhdrs[ 0].image))->cpu_signature;
			for (int n = 1; n < ucinfo->blobcount; ++n) {
				struct intel_hdrhdr_t *thdrhdr = &ucinfo->hdrhdrs[ n];
				
				union intel_SignatUnion *signatN = (union intel_SignatUnion *) 
				&((struct intel_uc_header_t *) (thdrhdr->image))->cpu_signature;
				uint32_t           flagsN  = ((struct intel_uc_header_t *) (thdrhdr->image))->cpu_flags;
				if (signat0->sigBitF.SteppingID       != signatN->sigBitF.SteppingID ||
						signat0->sigBitF.Model            != signatN->sigBitF.Model ||
						signat0->sigBitF.FamilyID         != signatN->sigBitF.FamilyID ||
						signat0->sigBitF.ProcessorType    != signatN->sigBitF.ProcessorType ||
						signat0->sigBitF.Model            != signatN->sigBitF.Model ||
						signat0->sigBitF.ExtendedModelID  != signatN->sigBitF.ExtendedModelID ||
						signat0->sigBitF.ExtendedFamilyID != signatN->sigBitF.ExtendedFamilyID ) {
					INFO( 0, "File %s: Blob 0 and %d have different cpu signatures!!\n", upfilepath, n + 1);
					r = -1;
					break;
				}
				if (cpu_flags_hit & flagsN) {
					// this warning indicates that here ucode rev or date will decide what blob to use
					INFO( 11, "Notice: Blob %d's cpu flags overlap with those of earlier ones!!\n", n + 1);
				} 
				cpu_flags_hit |= flagsN;
			}
		}
	}
	return r;
}


static char *
getdatestr( uint32_t datefield)
{
	static char datestr[ 11];
	/* create internal update file date, re-form from mmddyyyy to yyyymmdd */
	int m = datefield >> 24;
	int d = (datefield >> 16) & 0xff;
	int y = datefield & 0xffff;
	sprintf( datestr, "%04x/%02x/%02x", y, m, d);
	return datestr;
}


static void 
intel_printSignatInfo( uint32_t *sig_p, const char *ind)
{
	union intel_SignatUnion *sig = (union intel_SignatUnion *) sig_p;
	
	INFO( 10, "%sSignatureInt: %8X\n",
			ind, sig->sigInt);
	INFO( 11, "%s-> Family: %02X  Model: %02X  Stepping: %02X\n",
			ind, intel_getFamily( sig_p), intel_getModel( sig_p), sig->sigBitF.SteppingID);
	INFO( 12, "%sExtFamily: %02X  ExtModel: %02X\n",
			ind, sig->sigBitF.ExtendedFamilyID,
			sig->sigBitF.ExtendedModelID);
	INFO( 12, "%sIntFamily: %02X  IntModel: %02X\n",
			ind, sig->sigBitF.FamilyID,
			sig->sigBitF.Model);
	INFO( 10, "%sProcessorType:  %02X\n",
			ind, sig->sigBitF.ProcessorType);
}


static void 
intel_printExtSignatInfo( void *sig_p, const char *ind)
{
	union intel_ExtSignatUnion *sig = (union intel_ExtSignatUnion *) sig_p;
	intel_printSignatInfo( &sig->sigS.sig, ind);
	INFO( 10, "%sProcessor Flags:  0x%08X\n", ind, sig->sigS.cpu_flags);
}


static void
intel_printHeadersInfo( struct intel_hdrhdr_t *hdrhdr)
{
	struct intel_uc_header_t *hdr = (struct intel_uc_header_t *) hdrhdr->image;
	intel_printSignatInfo( &(hdr->cpu_signature), INDENT_0);
	INFO( 11, "%sDate %s\n", INDENT_0, getdatestr( hdr->date));
	INFO( 10, "%sucode rev  0x%08x\n", INDENT_0, hdr->revision);
	INFO( 12, "%sHeader ver 0x%08x\n", INDENT_0, hdr->header_version);
	INFO( 12, "%sLoader rev 0x%08x\n", INDENT_0, hdr->loader_revision);
	if (hdr->data_size) {
		INFO( 12, "%sData size  %d (0x%x)\n", INDENT_0, hdr->data_size, hdr->data_size);
	} else {
		// Intel spec says if data size field is 0 then data size == 2000
		INFO( 12, "%sData size  %d (0x%x) -->  %d (0x%x)\n", INDENT_0, hdr->data_size, hdr->data_size, 2000, 2000);
	}
		INFO( 10, "%sFlags      %d (0x%08x)\n", INDENT_0, hdr->cpu_flags, hdr->cpu_flags);
	if (!hdrhdr->has_ext_table) {
		INFO( 11, "%sHas no extended header.\n", INDENT_0);
	} else {
		INFO( 10, "%sExtended header information:\n", INDENT_1);
		INFO( 10, "%snum of Ext Signatures: %d\n", INDENT_1, hdrhdr->ext_header->sig_count);
		union intel_ExtSignatUnion *extsigp = hdrhdr->ext_table;
		for ( uint32_t i = 0; i < hdrhdr->ext_header->sig_count; i++) {
			INFO( 10, "%sExtended header entry %d:\n", INDENT_2, i);
			intel_printExtSignatInfo( extsigp++, INDENT_2);
		}
	}
} 


int
intel_printmicrocodestats( struct cpupdate_params *params)
{
	struct intel_ucinfo *ucinfo;
    struct intel_hdrhdr_t *thdrhdr;

	ucinfo = params->ucodeinfop;
    for (int n = 0; n < ucinfo->blobcount; ++n) {
    	thdrhdr = &ucinfo->hdrhdrs[ n];
    	INFO( 10, "Blob %d of %d headers info:\n", n + 1, ucinfo->blobcount);
    	intel_printHeadersInfo( thdrhdr);
	}
	return 0;
}


int
intel_update( struct cpupdate_params *params)
{
	struct intel_ProcessorInfo *pcoreinfo = (struct intel_ProcessorInfo *) params->coreinfop;
	struct intel_ucinfo *ucinfo = (struct intel_ucinfo *) params->ucodeinfop;
	struct intel_ProcessorInfo *coreinfo;
	char cpupath[ MAXPATHLEN];

	int core = 0;
	int r = 0;

	assert( pcoreinfo != NULL);
	assert( ucinfo != NULL);
	
	// walk each core and check update file for optimum blob
	for ( ; core < numCores ; ++core) {
//		struct intel_flagmatch        flagmatch;
//		flagmatch.headerindex = -1;
		struct intel_flagmatch        match;
//		flagmatch.headerindex = -1;

		coreinfo = pcoreinfo + core;
		
		// reload the core information, in case we have a faked core
		r = intel_getCoreInfo( coreinfo, core);
//			match.headerindex = -1;
		match.blobindex = -1;
		
		// If more than one blob matches the cpu flags, use the latest one
//		uint32_t cpu_flags_hit = ((struct intel_uc_header_t *) &ucinfo->hdrhdrs[ 0])->cpu_flags;
		struct intel_hdrhdr_t *thdrhdr;
		for (int n = 0; n < ucinfo->blobcount; ++n, ++thdrhdr) {
			thdrhdr = &ucinfo->hdrhdrs[ n];
			struct intel_uc_header_t *thdr = (struct intel_uc_header_t *) thdrhdr->image;
			if (coreinfo->flags & thdr->cpu_flags) {
				/* flags match. in case there were previous matches, 
				 * check which match is the more recent and keep that one.
				 */
				if (match.blobindex < 0) {
					/* first match */
/*					match.headerindex = 0;			/ *  apparently not used yet by Intel */
					match.blobindex = n;
					match.bestrev = thdr->revision;
				} else {
					/* second or higher match: check whether this match is more recent rev than previous one(s) */
					if ( match.bestrev < thdr->revision) {
/*						match.headerindex = 0;		/ *  apparently not used yet by Intel */
						match.blobindex = n;
						match.bestrev = thdr->revision;
					}
				}
#if 0
// extended headers apparently not used except once in 2014 for cpuid 406A8 and 406A9 so skipped this
					/* check extended headers also, if any */
					/* TODO: implement that if and when Intel introduces update files with extended headers */
#endif
			}
		}
		/* now do the core update.
		 * Many of the checks are redundant with previously done checks...
		 * anyway, this is better than too few checks :)
		 */
		if (match.blobindex < 0) {
			INFO( 11, "Core %d is up-to-date. Not updated.\n", core);
		} else {
			struct intel_hdrhdr_t *hdrhdr = &ucinfo->hdrhdrs[ match.blobindex];
			struct intel_uc_header_t *hdr = (struct intel_uc_header_t *) hdrhdr->image;
			struct cpuinfoBitF *ucf_sig = (struct cpuinfoBitF *) &(hdr->cpu_signature);
			// family, model and stepping must be identical, and the microcode revision 
			// of the update file must be higher than that of the processor
			int cpufd;
			
			sprintf( cpupath, "/dev/cpuctl%d", core);
	    	if (	coreinfo->sig.sigBitF.SteppingID		!= ucf_sig->SteppingID 			||
	    			coreinfo->sig.sigBitF.Model				!= ucf_sig->Model 				||
	    			coreinfo->sig.sigBitF.FamilyID			!= ucf_sig->FamilyID 			||
	    			coreinfo->sig.sigBitF.ProcessorType		!= ucf_sig->ProcessorType		||
	    			coreinfo->sig.sigBitF.ExtendedModelID	!= ucf_sig->ExtendedModelID		||
	    			coreinfo->sig.sigBitF.ExtendedFamilyID	!= ucf_sig->ExtendedFamilyID ) {
				INFO( 0, "Umm... update file %s should match, but somehow doesn't. Not updated.\n", params->filepath);
				r = -1;
			} else if (coreinfo->ucoderev >= hdr->revision) {
				INFO( 11, "Core %d is up-to-date. Not updated.\n", core);
			} else if (hdr->loader_revision != 1 || hdr->header_version != 1) {
				INFO( 0, "Cannot update core %d, need newer update method.\n", core);
				r = -1;
			} else if (!(hdr->cpu_flags & 0xff & coreinfo->flags)) {
				INFO( 0, "Processor flags do not match, cannot apply update.\n");
				r = -1;
			} else if (( r = ((cpufd = open( cpupath, O_RDWR)) < 0) )) {
				INFO( 0, "Failed to open %s for writing\n", cpupath);
			} else {
				cpuctl_update_args_t args;
	
				args.data = hdr + 1;
				args.size = hdrhdr->data_size;
				if (params->writeit) {
					r = ioctl( cpufd, CPUCTL_UPDATE, &args);
				} else {
					INFO( 12, "(Simulated only!) ");
					r = 0;
				}
				if (!r) {
					INFO( 11, "Updated core %d from microcode revision 0x%04x to 0x%04x\n", 
							core, coreinfo->ucoderev, hdr->revision);
				} else {
					INFO( 0, "Updating core %d failed!\n", core);
				}
			}
			if (cpufd >= 0)
				close( cpufd);
		}
	}
	return r;
}


int
intel_freeucodeinfo( struct cpupdate_params *params)
{
	struct intel_ucinfo 
			*ucinfo = (struct intel_ucinfo *) params->ucodeinfop;
	
	if (ucinfo != NULL) {
		if (ucinfo->image != NULL)
			free( ucinfo->image);
		free( ucinfo);
		params->ucodeinfop = NULL;
	}
	return 0;
}


int
intel_extractformat( struct cpupdate_params *params)
{
	struct intel_ucinfo 
			*ucinfo = (struct intel_ucinfo *) params->ucodeinfop;
	struct intel_uc_header_t *hdr;
    struct intel_hdrhdr_t 	*thdrhdr;
	union intel_SignatUnion  sig;
	char 					 opath[ MAXPATHLEN];
	int 					 blob = 0;
	int 					 r = 0;
	FILE					*ofp;
	assert( ucinfo != NULL);
	
	for ( ; r == 0 && blob < ucinfo->blobcount; ++blob) {
    	thdrhdr = &ucinfo->hdrhdrs[ blob];
		hdr = (struct intel_uc_header_t *) thdrhdr->image;
		sig.sigInt = hdr->cpu_signature;
		if (snprintf( opath, sizeof( opath), "%s/%02x-%02x-%02x-%x", params->targetdir, 
				 intel_getFamily( &hdr->cpu_signature),
				 intel_getModel( &hdr->cpu_signature),
				 sig.sigBitF.SteppingID,
				 hdr->cpu_flags) >= sizeof( opath)) {
			INFO( 0, "filename buffer too short for %s\n", opath);
			r = 1;
		} else {
			INFO( 10, "Writing output file %s from blob %d of %d\n", opath, blob, ucinfo->blobcount);
			ofp = fopen( opath, "w");
			if (ofp == NULL) {
				INFO( 0, "error opening output file %s\n", opath);
				r = 1;
			} else {
				if (fwrite( thdrhdr->image, thdrhdr->total_size, 1, ofp) < 1) {
					INFO( 0, "error writing to file %s\n", opath);
					r = 1;
				}
				if (fclose( ofp)) {
					INFO( 0, "error closing file %s\n", opath);
					r = 1;
				}
			}
		}
	}
	return r;
}


int
intel_compactformat( struct cpupdate_params *params)
{
	struct intel_ucinfo 
			*ucinfo = (struct intel_ucinfo *) params->ucodeinfop;
	struct intel_uc_header_t *hdr;
    struct intel_hdrhdr_t 	*thdrhdr;
	union intel_SignatUnion  sig;
	char 					 opath[ MAXPATHLEN];
	int 					 blob = 0;
	int 					 r = 0;
	FILE					*ofp;
	assert( ucinfo != NULL);
	
	if (ucinfo->blobcount > 1) {
		INFO( 10, "The file %s has more than 1 blob, skipped!\n", params->filepath);
	} else {
		thdrhdr = &ucinfo->hdrhdrs[ 0];
		hdr = (struct intel_uc_header_t *) thdrhdr->image;
		sig.sigInt = hdr->cpu_signature;
		if (snprintf( opath, sizeof( opath), "%s/%02x-%02x-%02x", params->targetdir, 
				 intel_getFamily( &hdr->cpu_signature),
				 intel_getModel( &hdr->cpu_signature),
				 sig.sigBitF.SteppingID) >= sizeof( opath)) {
			INFO( 0, "filename buffer too short for %s\n", opath);
			r = 1;
		} else {
			INFO( 10, "Appending blob %s\n...to output file %s\n", params->filepath, opath);
			ofp = fopen( opath, "ab");
			if (ofp == NULL) {
				INFO( 0, "error opening output file %s\n", opath);
				r = 1;
			} else {
				if (fwrite( thdrhdr->image, thdrhdr->total_size, 1, ofp) < 1) {
					INFO( 0, "error writing to file %s\n", opath);
					r = 1;
				}
				if (fclose( ofp)) {
					INFO( 0, "error closing file %s\n", opath);
					r = 1;
				}
			}
		}
	}
	return r;
}


const char *
intel_getvendorname( struct cpupdate_params *params)
{
	return VENDORNAME_INTEL;
}
