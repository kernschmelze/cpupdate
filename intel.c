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

#include "cpucommon.h"
#include "intel.h"
#include "cpupdate.h"

#define FUNC_printIntelUpdateFileStats   1
#define FUNC_IntelUpdate                 2
#define FUNC_IntelUpdateFromFile         4
#define FUNC_IntelUpdateFileVerify       8

void     intel_printSignatInfo(    uint32_t *sig_p);
void     intel_printExtSignatInfo( void *sig_p);
void     intel_printHeaderInfo( intel_fw_header_t *hdr, int data_size, int hasext, int payload_size);
void     intel_printHeadersInfo( intel_hdrhdr_t *hdrhdr);
int      intel_handleProcessor(    void *cpuinfo_p, int core, char *path, int func);
uint32_t intel_getFamily( uint32_t *sig);
uint32_t intel_getModel( uint32_t *sig);
int      getintfrombcd( uint32_t bcd);
int      checkdateplausibility( uint32_t datefield);
char    *getdatestr( uint32_t datefield);
int      intel_getHdrInfo( intel_hdrhdr_t *hdr);

uint32_t intel_getFamily( uint32_t *sig)
{
  intel_SignatUnion *sigp = (intel_SignatUnion *) sig;
  return sigp->signatBitF.ExtendedFamilyID + sigp->signatBitF.FamilyID;
}

uint32_t intel_getModel( uint32_t *sig)
{
  intel_SignatUnion *sigp = (intel_SignatUnion *) sig;
  return (sigp->signatBitF.ExtendedModelID << 4) + sigp->signatBitF.Model;
}

int getintfrombcd( uint32_t bcd)
{
  int dec = 0;
  int mult = 1;
  /* walk each nibble */
  for (int n, i = 0; i < 8; ++i) {
    n = bcd & 0xf;
    dec += n * mult;
    mult *= 10;
    bcd >>= 4;
  }
  return dec;
}

int checkdateplausibility( uint32_t datefield)
{
  uint32_t t = datefield;
  /* check nibbles for BCD range */
  for (int i = 0; i < 8; ++i) {
    if ((t & 0xf) > 9) return -1;
    t >>= 4;
  }
  /* create internal update file date, re-form from mmddyyyy to yyyymmdd */
  int m = datefield >> 24;
  int d = (datefield >> 16) & 0xff;
  int y = datefield & 0xffff;
  int md = getintfrombcd( m);
  int dd = getintfrombcd( d);
  int yd = getintfrombcd( y);
  if (md > 12) return -1;
  if (dd > 31) return -1;   /* XXX table for month lengths, leap years? */
  if (yd < 1995) return -1;
  /* TODO check for date in future */
  return 0;  
}

char *getdatestr( uint32_t datefield)
{
  static char datestr[ 11];
  /* create internal update file date, re-form from mmddyyyy to yyyymmdd */
  int m = datefield >> 24;
  int d = (datefield >> 16) & 0xff;
  int y = datefield & 0xffff;
  sprintf( datestr, "%04x/%02x/%02x", y, m, d);
  return datestr;
}

/* See https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf
 * page 3-204, Figure 3-6
 */
int intel_probe( void)
{
  char vendor[ MAXVENDORNAMELEN];
  cpuctl_cpuid_args_t idargs = {
    .level  = 0,
  };
  const char *cpudev = "/dev/cpuctl0";
  int cpufd;
  int r;
  
  if ((r = ((cpufd = open( cpudev, O_RDONLY)) < 0)))
    INFO( 0, "error opening %s for reading\n", cpudev);
  if (!r && ((r = ((ioctl( cpufd, CPUCTL_CPUID, &idargs) < 0)))))
    INFO( 0, "ioctl( CPUCTL_CPUID) failed\n");
  if (!r) {
    ((uint32_t *)vendor)[0] = idargs.data[1];
    ((uint32_t *)vendor)[1] = idargs.data[3];
    ((uint32_t *)vendor)[2] = idargs.data[2];
    vendor[12] = '\0';
    r = (strncmp( vendor, INTEL_VENDOR_ID, sizeof( INTEL_VENDOR_ID))) ? 1 : 0;
  } else
    r = -1;
  return r;
}

void intel_printSignatInfo( uint32_t *sig_p)
{
  intel_SignatUnion *sig = (intel_SignatUnion *) sig_p;
  
  INFO( 11, "ProcessorType:  %02X\n",
        sig->signatBitF.ProcessorType);
  INFO( 12, "ExtFamily: %02X  ExtModel: %02X\n",
        sig->signatBitF.ExtendedFamilyID,
        sig->signatBitF.ExtendedModelID);
  INFO( 12, "IntFamily: %02X  IntModel: %02X\n",
        sig->signatBitF.FamilyID,
        sig->signatBitF.Model);
  INFO( 12, "SignatureInt: %8X\n",
        sig->signatInt);
  if (outputmode & (OUTPMODE_F | OUTPMODE_C)) {
    INFO( 10, "-> Family: %02X  Model: %02X  Stepping: %02X\n",
          intel_getFamily( sig_p), intel_getModel( sig_p), sig->signatBitF.SteppingID);
} }

void intel_printExtSignatInfo( void *sig_p)
{
  intel_ExtSignatUnion *sig = (intel_ExtSignatUnion *) sig_p;
  intel_printSignatInfo( &sig->sigS.sig);
  INFO( 10, "Processor Flags:  0x%08X\n", sig->sigS.cpu_flags);
}

void intel_printHeadersInfo( intel_hdrhdr_t *hdrhdr)
{
  intel_fw_header_t *hdr = (intel_fw_header_t *) hdrhdr->image;
  INFO( 11, "    Date %s\n", getdatestr( hdr->date));
  INFO( 10, "    ucode rev  0x%08x\n", hdr->revision);
  INFO( 12, "    Header ver 0x%08x\n", hdr->header_version);
  INFO( 12, "    Loader rev 0x%08x\n", hdr->loader_revision);
  INFO( 12, "    Data size  %d (0x%x)\n", hdr->data_size, hdr->data_size);
  intel_printSignatInfo( &(hdr->cpu_signature));
  INFO( 12, "    Flags      %d (0x%08x)\n", hdr->cpu_flags, hdr->cpu_flags);
  if (!hdrhdr->has_ext_table) {
    INFO( 11, "Has no extended header.\n");
  } else {
    INFO( 10, "    Extended header information:\n");
    INFO( 10, "    num of Ext Signatures: %d\n", hdrhdr->ext_header->sig_count);
    intel_ExtSignatUnion *extsigp = hdrhdr->ext_table;
    for ( uint32_t i = 0; i < hdrhdr->ext_header->sig_count; i++) {
      INFO( 10, "      Extended header entry %d:\n", i);
      intel_printExtSignatInfo( extsigp++);
} } } 

void intel_printProcessorInfo( void *cpuinfo_p, int core)
{
  intel_ProcessorInfo *cpuinfo = (intel_ProcessorInfo *) cpuinfo_p;
  
  INFO( 10, "Processor Core: %d\n", core);
  intel_printSignatInfo( &(cpuinfo->esig.sigS.sig));
  INFO( 11, "Flags: %d\n",
        cpuinfo->flags);
  INFO( 10, "-> CPUID: %x  Family: %02X  Model: %02X  Stepping: %02X  uCodeRev: %08X\n",
        cpuinfo->esig.sigS.sig,
        cpuinfo->family, 
        cpuinfo->model,
        cpuinfo->esig.sigU.signatBitF.SteppingID,
        cpuinfo->ucoderev
      );
}
    
int intel_getProcessorInfo( void *cpuinfo_p, int core)
{
  intel_ProcessorInfo *cpuinfo = (intel_ProcessorInfo *) cpuinfo_p;
  int                 r, cpufd = -1;
  cpuctl_msr_args_t   msrargs;
  cpuctl_cpuid_args_t idargs = {
    .level  = 1,  /* Signature. */
  };
  char cpudev[ MAXPATHLEN];
  
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
      cpuinfo->flags = 1 << ((msrargs.data >> 50) & 7);
  } }
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
    cpuinfo->esig.sigS.sig       = idargs.data[0];
    cpuinfo->esig.sigS.cpu_flags = idargs.data[1];
    cpuinfo->esig.sigS.checksum  = idargs.data[2];
    if ((r = ioctl( cpufd, CPUCTL_RDMSR, &msrargs)) < 0) {
      INFO( 0, "%s MSR read failed\n", cpudev);
    } else
      r = 0;
  } 
  if (!r) {
    msrargs.msr = MSR_BIOS_SIGN;
    if ((r = ioctl(cpufd, CPUCTL_RDMSR, &msrargs)) < 0) {
      INFO( 0, "%s signature read failed\n", cpudev);
  } }
  if (!r) {
    /* Micorocode revision in the high dword. See Intel Manual Vol 3A, pg 9-36. */
    cpuinfo->ucoderev = msrargs.data >> 32; 
    /* From https://software.intel.com/en-us/articles/intel-architecture-and-processor-identification-with-cpuid-model-and-family-numbers
     * The Family number is an 8-bit number derived from the processor 
     * signature by adding the Extended Family number (bits 27:20) and 
     * the Family number (bits 11:8). 
     */
    cpuinfo->family = intel_getFamily( &cpuinfo->esig.sigU.signatInt);
    cpuinfo->model = intel_getModel( &cpuinfo->esig.sigU.signatInt);
    INFO( 12, "%s identification successful!\n", cpudev);
  }
  if (cpufd >= 0) close( cpufd);
  return r;
}

/* populates the hdrhdr structure while validating the blob.
 * hdr-> image must be preset to point at the blob start address,
 * this saves us an argument
 */
int intel_getHdrInfo( intel_hdrhdr_t *hdr)
{
  int r = 0;
  intel_fw_header_t *image = (intel_fw_header_t *) hdr->image;
  
  /* check if the [first] header looks valid at the first glimpse */
  if (!r) {
    if (image->header_version != INTEL_HEADER_VERSION ||
        image->loader_revision != INTEL_LOADER_REVISION) {
      INFO( 0, "Unsupported version\n");
      r = -1;
  } }
  if (!r) {
    /* According to spec, if data_size == 0, then size of ucode = 2000. */
    hdr->data_size = (image->data_size == 0) ? 2000 : image->data_size;
    /* spec does not mention that if data size field is zero, total size field is also zero */
    hdr->total_size = (image->data_size == 0 && image->total_size == 0) 
                 ? (hdr->data_size + sizeof( intel_fw_header_t)) : image->total_size;
    /* data size must be a multiple number of dwords (4 byte) */
    if (hdr->data_size % sizeof( uint32_t)) {
      INFO( 0, "Data size is not multiple of dword\n");
      r = -1;
  } }
  if (!r) {
    hdr->payload_size = hdr->data_size + sizeof( intel_fw_header_t);
  }
  if (!r) {
    uint32_t sum = 0;
    uint32_t *p = (uint32_t *) image;
    int n = hdr->total_size / sizeof( uint32_t);
    for (int i = 0; i < n; i++, p++)
      sum += *p;
    /* checksum (sum) must be zero. image->checksum only serves to get it zero */
    if (sum) {
      INFO( 0, "Image's primary checksum invalid\n");
      r = -1;
  } }
  /* if present, now examine extended headers */
  hdr->has_ext_table = 0;   /* default if not changed in the following checks */
  if (!r && (hdr->total_size > hdr->payload_size)) {
    /* does image have an extended signature table? */
    hdr->ext_size = hdr->total_size - hdr->payload_size;
    hdr->has_ext_table = 1;
    /* extended headers were introduced with family 0FH, model 03H. (see vol 3A, pg 9-28) */
    if ( intel_getFamily( &image->cpu_signature) < 0x0f ||
        (intel_getFamily( &image->cpu_signature) == 0x0f && intel_getModel( &image->cpu_signature) < 0x03) ) {
      INFO( 0, "Extended header present, but not supported with that family/model\n");
      r = -1;
  } } 
  if (!r && hdr->has_ext_table) {
    if (hdr->ext_size < (signed) sizeof( intel_ext_header_t)) {
      INFO( 0, "Image's extended header incomplete\n");
      r = -1;
  } }
  if (!r && hdr->has_ext_table) {
    hdr->ext_header = (intel_ext_header_t *) (((uint8_t *) image) + hdr->payload_size);
    hdr->ext_table = (intel_ExtSignatUnion *) (hdr->ext_header + 1);
    /* Check the extended table size. */
    hdr->ext_table_size = sizeof( intel_ext_header_t) + hdr->ext_header->sig_count * sizeof( intel_ExtSignatUnion);
    if (hdr->ext_table_size + hdr->payload_size > hdr->total_size) {
      INFO( 0, "Extended signature table incomplete\n");
      r = -1;
  } } 
  if (!r && hdr->has_ext_table) {
    uint32_t sum = 0;
    uint32_t *p = (uint32_t *) hdr->ext_table;
    int n = hdr->ext_table_size / sizeof( uint32_t);
    for ( int i = 0; i < n; i++, p++)
      sum += *p;
    if ((r = sum)) {
      INFO( 0, "Extended signature table checksum invalid");
      r = -1;
  } }
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
      intel_fw_header_t *cimagehdr = (intel_fw_header_t *) cimage;
      intel_ExtSignatUnion *extsig = hdr->ext_table + en;
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
        INFO( 0, "Image's extended blob #%d checksum invalid\n", en);
        r = -1;
  } } }
  return r;
}
  
void intel_printUpdateFileStats( char *path)
{
  intel_handleProcessor( NULL, 0, path, FUNC_printIntelUpdateFileStats);
}

int intel_updateProcessor( void *cpuinfo, int core, char *path)
{
  return intel_handleProcessor( cpuinfo, core, path, FUNC_IntelUpdate);
}

int intel_updateProcessorFromFile( void *cpuinfo, int core, char *path)
{
  return intel_handleProcessor( cpuinfo, core, path, FUNC_IntelUpdateFromFile);
}

int intel_verifyUpdfInteg( char *path, char *fnamp)
{
  return intel_handleProcessor( NULL, 0, path, FUNC_IntelUpdateFileVerify);
}

/* void printIntelUpdateFileStats( char *fpath)  */
int intel_handleProcessor( void *cpuinfo_p, int core, char *path, int func)
{
  int                    updfd, r = 0;
  struct stat            st;
  uint8_t               *fw_image = MAP_FAILED; /* init as NULL resp MAP_FAILED */
  /* the following vars used only for updating */
  intel_ProcessorInfo   *cpuinfo;
  cpuctl_update_args_t   args;
  char                   fnam[ MAXPATHLEN];
  char                   cpupath[ MAXPATHLEN];
  char                   updatefpath[ MAXPATHLEN];
  char                   fpath[ MAXPATHLEN];
  intel_hdrhdr_t         hdrhdrs[ MAXHEADERS];
  intel_flagmatch        flagmatch;
  flagmatch.headerindex = -1;
  int                    blobcount = 0;
  intel_flagmatch        match;

  if (func & (FUNC_IntelUpdate | FUNC_IntelUpdateFromFile)) {
    /* In this block, get processor stats and form a filename.
     * After this block, continue with reading and verifying it.
     * When the update file has been verified, continue at the 
     * end of the function with another update section which
     * updates the particular core.
     */
    /* first init match struct to no match */
    match.headerindex = -1;
    match.blobindex = -1;
    
    cpuinfo = (intel_ProcessorInfo *) cpuinfo_p;
    r = intel_getProcessorInfo( cpuinfo, core);
    if (r) {
      INFO( 0, "Failed to get processor info for core %d!\n", core);
    } else {
      sprintf( cpupath, "/dev/cpuctl%d", core);
      if (func & FUNC_IntelUpdate) {
        /* construct family-model-stepping filename for microcode binary */
        sprintf( fnam, "%02x-%02x-%02x", cpuinfo->family, cpuinfo->model, cpuinfo->esig.sigU.signatBitF.SteppingID);
        INFO( 11, "Update filename: %s!\n", fnam);
        sprintf( updatefpath, "%s/%s", path, fnam);
        strcpy( fpath, updatefpath);
      } else if (func & FUNC_IntelUpdateFromFile) {
        strcpy( fpath, path);
  } } } else if (func & (FUNC_printIntelUpdateFileStats | FUNC_IntelUpdateFileVerify)) {
    strcpy( fpath, path);
  }
  /* now the shared section: read update file name and verify its format is valid */
  if (!r && (r = ((updfd = open(fpath, O_RDONLY, 0)) < 0))) {
    INFO( 0, " Failed to open %s file\n", fpath);
  } else 
    r = 0;
  if (!r && ((r = fstat( updfd, &st))))
    INFO( 0, "File %s fstat failed\n", fpath);
  if (!r && (st.st_size < 0 || (unsigned) st.st_size < sizeof( intel_fw_header_t))) {
    INFO( 0, "File %s is shorter than update file header size\n", fpath);
    r = -1;
  }
  if (!r) {
    /* mmap the whole image.*/
    fw_image = (uint8_t *) mmap( NULL, st.st_size, PROT_READ, MAP_PRIVATE, updfd, 0);
    if ((r = (fw_image == MAP_FAILED))) {
      INFO( 0, "File %s mmaping failed\n", fpath);
  } }
  /* Loading update file part finished, now identify and verify it */
  /* get first header to get the file's basic information */  
  if (!r) {
    hdrhdrs[ 0].image = fw_image;
  }
  if (!r && (r = intel_getHdrInfo( &hdrhdrs[ 0])))
    INFO( 0, "Error in [first] header\n");
  /* check how many updates the file contains [usually each for different processor flags, up to 8] */
  if (!r && ((&hdrhdrs[ 0])->total_size != st.st_size)) {
    /* NOTE: let's for now assume all blobs are the same size.
     * This must be re-checked, as it's not sure if that's the spec.
     * Maybe the header scan algorithm in this block needs to be changed. XXX
     */
    if (st.st_size % (&hdrhdrs[ 0])->total_size) {
      INFO( 0, "Total size reported in header is not a multiple of actual update blob size\n");
      r = -1;
  } } 
  if (!r) {
    blobcount = st.st_size / (&hdrhdrs[ 0])->total_size;
    if (blobcount > MAXHEADERS) {
      INFO( 0, "Contains %d headers, but only %d are supported!\n", blobcount, MAXHEADERS);
      r = -1;
  } }
  if (!r) {
    /* now walk through every blob, check its validity and put its "metadata" 
     * into the hdrs[] array using pointers
     */
    INFO( 12, "File %s contains %d update blobs\n", fpath, blobcount);
    intel_hdrhdr_t *thdrhdr;
    for (int n = 0; n < blobcount; ++n) {
      thdrhdr = &hdrhdrs[ n];
      thdrhdr->image = fw_image + (n * (&hdrhdrs[ 0])->total_size);
      r = intel_getHdrInfo( thdrhdr);
      if (r) {
        INFO( 0, "Header/Blob %d of %d seems to be inconsistent!\n", n + 1, blobcount);
        break;
      } 
      if (!r && thdrhdr->has_ext_table) {
        INFO( 12, "Header/Blob %d of %d has extended header!\n", n + 1, blobcount);
  } } }
  /* verify that there is no conflicting/ambiguous situation that makes matching correct update impossible
   *    -headers should all have same cpuid but different flags
   *    -if there are identical ones, the revision id could remove ambiguity TODO XXX
   */
  if (!r && blobcount > 1) {
    /* only if multiple blobs present: check the ones beyond the beginning one */
    uint32_t cpu_flags_hit     = ((intel_fw_header_t *) (hdrhdrs[ 0].image))->cpu_flags;
    intel_SignatUnion *signat0 = (intel_SignatUnion *) &((intel_fw_header_t *) (hdrhdrs[ 0].image))->cpu_signature;
    for (int n = 1; n < blobcount; ++n) {
      intel_hdrhdr_t *thdrhdr = &hdrhdrs[ n];
      
      intel_SignatUnion *signatN = (intel_SignatUnion *) &((intel_fw_header_t *) (thdrhdr->image))->cpu_signature;
      uint32_t           flagsN  = ((intel_fw_header_t *) (thdrhdr->image))->cpu_flags;
      if (signat0->signatBitF.SteppingID       != signatN->signatBitF.SteppingID ||
          signat0->signatBitF.Model            != signatN->signatBitF.Model ||
          signat0->signatBitF.FamilyID         != signatN->signatBitF.FamilyID ||
          signat0->signatBitF.ProcessorType    != signatN->signatBitF.ProcessorType ||
          signat0->signatBitF.Model            != signatN->signatBitF.Model ||
          signat0->signatBitF.ExtendedModelID  != signatN->signatBitF.ExtendedModelID ||
          signat0->signatBitF.ExtendedFamilyID != signatN->signatBitF.ExtendedFamilyID ) {
        INFO( 0, "Header 0 and %d have different cpu signatures!!\n", n + 1);
        r = -1;
        break;
      }
      if (thdrhdr->has_ext_table) {
        // TODO walk extended headers and check signatures if any
        // TODO walk extended headers and check flags if any
        INFO( 0, "Header %d has extended header - not checked!!\n", n + 1);
      } 
      if (cpu_flags_hit & flagsN) {
        INFO( 12, "Notice: Header %d's cpu flags overlap with those of earlier ones!!\n", n + 1);
      } 
      cpu_flags_hit |= flagsN;
  } }
  /* now check for matching header(s) if in update mode.
   * If more than one blob matches the cpu flags, use the latest one
   */
  if (!r && blobcount >= 1 && func & (FUNC_IntelUpdate | FUNC_IntelUpdateFromFile)) {
    match.headerindex = -1;
    match.blobindex = -1;
    uint32_t cpu_flags_hit = ((intel_fw_header_t *) &hdrhdrs[ 0])->cpu_flags;
    intel_hdrhdr_t *thdrhdr;
    for (int n = 0; n < blobcount; ++n, ++thdrhdr) {
      thdrhdr = &hdrhdrs[ n];
      intel_fw_header_t *thdr = (intel_fw_header_t *) thdrhdr->image;
      if (cpuinfo->flags & thdr->cpu_flags) {
        /* flags match. in case there were previous matches, 
         * check which match is the more recent and keep that one.
         */
        if (match.blobindex < 0) {
          /* first match */
          match.headerindex = 0;       /* XXX apparently not used yet by Intel */
          match.blobindex = n;
          match.bestrev = thdr->revision;
        } else {
          /* second or higher match: check whether this match is more recent rev than previous one(s) */
          if ( match.bestrev < thdr->revision) {
            match.headerindex = 0;       /* XXX apparently not used yet by Intel */
            match.blobindex = n;
            match.bestrev = thdr->revision;
        } }
        /* check extended headers also, if any */
        /* XXXTODO: do that if and when Intel introduces update files with extended headers */
  } } }
  if (!r && outputmode & OUTPMODE_F) {
    /* file information output commanded */
    intel_hdrhdr_t *thdrhdr;

    for (int n = 0; n < blobcount; ++n) {
      thdrhdr = &hdrhdrs[ n];
      INFO( 10, "  Blob %d of %d headers info:\n", n + 1, blobcount);
      intel_printHeadersInfo( thdrhdr);
  } }

  if (!r && func & (FUNC_IntelUpdate | FUNC_IntelUpdateFromFile)) {
    /* now do the update section which updates the particular core, 
     * if update file is valid and matches the core.
     * Some of the checks are redundant with above checks...
     * anyway, this is better than too few checks :)
     */
    int matchind = (blobcount > 1) ? match.blobindex : 0;
    intel_hdrhdr_t *hdrhdr = &hdrhdrs[ matchind];
    intel_fw_header_t *hdr = (intel_fw_header_t *) hdrhdrs->image;
    intel_SignatUnion *hdr_sig = (intel_SignatUnion *) &(hdr->cpu_signature);
    /* family, model and stepping must be identical, and the microcode revision 
     * of the update file must be higher than that of the processor
     */
    uint32_t fw_family = hdr_sig->signatBitF.ExtendedFamilyID + hdr_sig->signatBitF.FamilyID;
    uint32_t fw_model = (hdr_sig->signatBitF.ExtendedModelID << 4) + hdr_sig->signatBitF.Model;
    if (cpuinfo->family == fw_family && cpuinfo->model == fw_model && 
                        cpuinfo->esig.sigU.signatBitF.SteppingID == hdr_sig->signatBitF.SteppingID) {
      if (cpuinfo->ucoderev < hdr->revision) {
        if (hdr->loader_revision == INTEL_LOADER_REVISION && hdr->header_version == INTEL_HEADER_VERSION) {   /* XXXX */
          if (hdr->cpu_flags & 0xff & cpuinfo->flags) {
            int cpufd;
            if (( r = ((cpufd = open( cpupath, O_RDWR)) < 0) )) {
              INFO( 0, "Failed to open %s for writing\n", cpupath);
            } else {
              args.data = hdr + 1;
              args.size = hdrhdr->data_size;
              if (writeit) {
                r = ioctl( cpufd, CPUCTL_UPDATE, &args);
              } else {
                INFO( 0, "(Simulated only!) ");
                r = 0;
              }
              if (!r) {
                INFO( 10, "Updated core %d from microcode revision 0x%04x to 0x%04x\n", core, cpuinfo->ucoderev, hdr->revision);
              } else {
                INFO( 0, "Updating core %d failed!\n", core);
            } }
            if (cpufd >= 0)
              close( cpufd);
          } else {
            INFO( 0, "Processor flags do not match, cannot apply update.\n");
            r = -1;
        } } else {
          INFO( 0, "Cannot update core %d, need newer update method.\n", core);
          r = -1;
      } } else 
        INFO( 10, "Core %d is up-to-date, not updated.\n", core);
    } else
      INFO( 0, "Umm... update file %s should match, but doesn't. Not updated.\n", fpath);
  } 

  if (func & FUNC_IntelUpdateFileVerify) {
    /* implied if (outputmode & OUTPMODE_F) */
     if (!r) {
       INFO( 0, "Update file %s seems to be valid.\n", fpath);
    } else {
       INFO( 0, "Update file %s failed verification.\n", fpath);
  } }
  if (fw_image != MAP_FAILED && munmap(fw_image, st.st_size)) {
    INFO( 0, "munmap(%s) failed\n", fpath);
    r = -1;
  }
  if (updfd >= 0)
    close( updfd);
  return r;
}
    

