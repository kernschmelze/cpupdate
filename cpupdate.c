/*-Copyright (c) 2018 Stefan Blachmann <sblachmann at gmail.com>
 * Using parts of cpucontrol.c, which is...
 * Copyright (c) 2008-2011 Stanislav Sedov <stas@FreeBSD.org>.
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
#include <sysexits.h>
#include <dirent.h>

#include <sys/queue.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/cpuctl.h>

#include "cpucommon.h"
#include "intel.h"
#include "cpupdate.h"


int  verbosity = 10;
int  writeit = 0;
int  outputmode = 0;
int  extractbuggedfiles = 0;    /* extract files containing multiple blobs - calling them "bugged" because they violate Intel's own spec */
char extractdir[ MAXPATHLEN];
int  vendormode = -1;
static int numCores = 0;

#define VENDOR_INDEX_INTEL 0
#define VENDOR_INDEX_AMD   1
#define VENDOR_INDEX_VIA   2

static cpu_handler_t handlers[] = {
  { "Intel", intel_probe, intel_getProcessorInfo, intel_printProcessorInfo, intel_printUpdateFileStats, intel_updateProcessor, intel_updateProcessorFromFile, intel_verifyUpdfInteg }
};
static cpu_handler_t *handler;
static cpuinfobuf_t   cpuinfobuf;

static void usage( void);
int cpu_getCoreNum( void);
int cpu_getBrand( void);

void usage( void)
{
  const char *name;

  if ((name = getprogname()) == NULL)
    name = "<programname>";
  fprintf(stderr, "Usage: %s [-qwvviIAVh] [-<f|U> <microcodefile>] [-<u|c> <datadir>] [-x <dirname>]\n", name);
  fprintf(stderr, "  -i   show processor information\n");
  fprintf(stderr, "  -f   show version information of microcode file\n");
  fprintf(stderr, "  -u   update microcode using microcode files in <datadir>\n");
  fprintf(stderr, "  -U   update microcode using file <microcodefile>\n");
  fprintf(stderr, "  -w   write it: without this option cpupdate only simulates updating\n");
  fprintf(stderr, "  -c   Check integrity of microcode files in <datadir>. Vendor mode must be set!\n");
  fprintf(stderr, "  -IAV for -c and f options: set vendor mode to Intel/AMD/VIA [atm only Intel implemented]\n");
  fprintf(stderr, "  -x   extract microcode files from multi-blob intel-ucode files to <dirname>\n");
  fprintf(stderr, "  -q   quiet mode\n");
  fprintf(stderr, "  -v   verbose mode, -vv very verbose \n");
  fprintf(stderr, "  -h   show this help\n");
  exit(EX_USAGE);
}

int cpu_getCoreNum()
{
  struct dirent *direntry;
  DIR *dirp = opendir("/dev");
  int r = 0;
  int high = 0;
  
  if (dirp == NULL) {
    r = -1;
  } else {
    while ((direntry = readdir(dirp)) != NULL) {
      if (direntry->d_namlen == 0)
        continue;
      if (!strncmp( direntry->d_name, "cpuctl", 6)) {
        int x = atoi( direntry->d_name + 6);
        if (x > high)
          high = x;
    } }
    r = closedir( dirp);
  }
  return (r) ? -1 : ++high;
}

int cpu_getBrand()
{
  unsigned int i;
  int          r = -1;
  
  for (i = 0; i < NHANDLERS; i++)
    if ((r = handlers[i].probe()) == 0)
      break;
  if (r >= 0 && i < NHANDLERS) {
    handler = &handlers[i];
    r = i;
  } else
    INFO(10, "Sorry! This CPU brand is unknown to me.\nDid you do 'kldload cpuctl'?\n");
  return r;
}

static int isdir( const char *path)
{
  int r;
  struct stat st;

  r = stat( path, &st);
  if (r < 0)
    INFO( 0, "stat(%s) failed\n", path);
  return (r < 0) ? r : (st.st_mode & S_IFDIR);
}

int main(int argc, char *argv[])
{
  int   c, cmd, r = 0;
  char *data;
  int   ambigc = 0;
  int   ambigv = 0;

  if (argc == 1)
    usage();
  while ((c = getopt( argc, argv, "IAVc:f:ihqu:U:vwx:")) != -1) {
    switch (c) {
      case 'q': verbosity -= 10;
                break;
      case 'v': ++verbosity;
                break;
      case 'w': ++writeit;
                break;
      case 'x': if (extractbuggedfiles || ambigc) {
                  INFO( 0, "ERROR: -x option allowed only once, and none of the [cfuIih] options allowed \n");
                  usage();
                }
                ++extractbuggedfiles;
                strcpy( extractdir, optarg);
                break;
      case 'c': 
      case 'f': 
      case 'u':
      case 'U': data = optarg;
      case 'i': 
      case 'h': cmd = c;
                if (extractbuggedfiles || ambigc) {
                  INFO( 0, "ERROR: only one of the [cfuIihx] options allowed\n");
                  usage();
                }
                ambigc = 1;
                break;
      case 'I': vendormode = VENDOR_INDEX_INTEL;
                if (ambigv) {
                  INFO( 0, "ERROR: only one vendor mode option allowed\n");
                  usage();
                }
                ambigv = 1;
                break;
/*
      case 'A': vendormode = VENDOR_INDEX_AMD;
                if (ambigv) {
                  INFO( 0, "ERROR: only one vendor mode option allowed\n";
                  usage();
                }
                ambigv = 1;
                break;
      case 'V': vendormode = VENDOR_INDEX_VIA;
                if (ambigv) {
                  INFO( 0, "ERROR: only one vendor mode option allowed\n";
                  usage();
                }
                ambigv = 1;
                break;
*/              
      default:  usage();
  } }
  switch (cmd) {
    case 'i': outputmode = OUTPMODE_I;
              numCores = cpu_getCoreNum();
              if (numCores < 1) {
                INFO( 0, "Failed to determine number of cores. Did you do 'kldload cpuctl'?\n");
              } else {
                r = cpu_getBrand();
                if (r >= 0) {
                  INFO( 10, "Found CPU(s) from %s\n", handler->vendorName);
                  for ( int i = 0; i < numCores; ++i) {
                    r = handler->getInfo( &cpuinfobuf, i);
                    if (!r) {
                      handler->printInfo( &cpuinfobuf, i);
                    } else {
                      INFO( 0, "Failed to get info about core %d\n", i);
              } } } }
              break;
    case 'U': 
    case 'u': outputmode = OUTPMODE_U;
              numCores = cpu_getCoreNum();
              if (numCores < 1) {
                INFO( 0, "Failed to determine number of cores. Did you do 'kldload cpuctl'?\n");
                r = -1;
              } else {
                r = cpu_getBrand();
                if (r >= 0) {
                  INFO( 11, "Found CPU(s) from %s\n", handler->vendorName);
                  for ( int i = 0; i < numCores; ++i) {
                    if (cmd == 'u') {
                      r = handler->update( &cpuinfobuf, i, data);
                    } else {
                      r = handler->updateFromFile( &cpuinfobuf, i, data);
                    }
                    if (r) {
                      INFO( 0, "Failed to update core %d\n", i);
                      r = -1;
                      break;
              } } } }
              break;
    case 'f': outputmode = OUTPMODE_F;
              if (vendormode < 0) {
                INFO( 0, "ERROR: vendor mode option missing\n");
                usage();
              } else
                handler = &handlers[ vendormode];
              handler->printUpdFStats( data);
              break;
    case 'c': outputmode = OUTPMODE_C;
              if (vendormode < 0) {
                INFO( 0, "ERROR: vendor mode option missing\n");
                usage();
              } else
                handler = &handlers[ vendormode];
              {
                DIR *dirp;
                struct dirent *direntry;
                char fpath[ MAXPATHLEN];
                
                dirp = opendir( data);
                if (dirp == NULL) {
                  INFO( 0, "Failed to access directory %s\n", data);
                } else {
                  while ((direntry = readdir( dirp)) != NULL) {
                    if (direntry->d_namlen == 0)
                      continue;
                    ;
                    if (snprintf( fpath, sizeof( fpath), "%s/%s", data, direntry->d_name) >= (signed int) sizeof( fpath)) {
                      INFO( 0, "skipping %s, filename buffer too short\n", direntry->d_name);
                      continue;
                    }
                    if (isdir( fpath) != 0) {
                      INFO( 0, "skipping %s: is a directory\n", fpath);
                      continue;
                    }
                    handler->verifyUpdF( fpath, direntry->d_name);
              } } }
              break;
    case 'h': 
    default : usage();
  }
  return r;
}
