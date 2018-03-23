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
#include <sys/linker.h>
#include <sys/module.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/cpuctl.h>

#include "cpupdate.h"
#include "intel.h"

// extern, set in cpu_getCoreNum() and used by the 
// vendor-specific funcs to walk through cores
int		verbosity = 10;
int		vendormode = -1;
int		numCores = 0;

#define VENDOR_INDEX_INTEL 0
#define VENDOR_INDEX_AMD   1
#define VENDOR_INDEX_VIA   2

static struct	vendor_funcs   *handler;
static struct	cpupdate_params	cpupbuf;

char *pgmn = "cpupdate";			// program name for messages in case programname() does not work
static struct vendor_funcs *handlers[] = {
	&intel_funcs
  // other handlers, AMD, VIA here
};

static void usage( void);
static int modload( const char *name);
static int cpu_getCoreNum( void);
static int cpu_setHandler( void);
// leave the switch in to make cpupdate work on older FreeBSD versions 
// without Meltdown/Spectr mitigations, too
#ifdef CPUCTL_EVAL_CPU_FEATURES
static int do_eval_cpu_features( const char *dev);
#endif

void 
usage( void)
{
  fprintf(stderr, "Usage: %s [-qwvvuiCXIAVh] [-<f|U> <microcodefile>] [-<cpsST> <datadir>]\n", pgmn);
  fprintf(stderr, "  -i   show processor information\n");
  fprintf(stderr, "  -f   show version information of microcode file\n");
  fprintf(stderr, "  -u   update microcode\n");
  fprintf(stderr, "  -U   update microcode using file <microcodefile>\n");
  fprintf(stderr, "  -w   write it: without this option cpupdate only simulates updating\n");
  fprintf(stderr, "  -q   quiet mode\n");
  fprintf(stderr, "  -v   verbose mode, -vv very verbose\n");
  fprintf(stderr, "  -p   use primary repo path <datadir>\n");
  fprintf(stderr, "  -s   use secondary repo path <datadir>\n");
  fprintf(stderr, "  -V   print version\n");
  fprintf(stderr, "  -h   show this help\n");
  fprintf(stderr, "  -IAV for the options below: set vendor mode to Intel/AMD/VIA [atm only Intel implemented]\n");
  fprintf(stderr, "  -c   Check integrity of microcode files in <datadir>. Vendor mode must be set!\n");
  fprintf(stderr, "  -C   convert/compact microcode files from legacy to multi-blob intel-ucode file format\n");
  fprintf(stderr, "  -X   convert (extract) microcode files from multi-blob intel-ucode to legacy file format\n");
  fprintf(stderr, "  -S   source dir for converting\n");
  fprintf(stderr, "  -T   target dir for converting\n");
  exit(EX_USAGE);
}


static int
modload( const char *name)
{
	if (modfind(name) < 0)
		if (kldload(name) < 0 || modfind(name) < 0) {
			warn("%s: module not found", name);
			return 0;
		}
	return 1;
}


static int 
cpu_getCoreNum( void)
{
	struct dirent *direntry;
	DIR *dirp = opendir("/dev");
	int r = 0;
	int high = 0;
	
	if (dirp == NULL) {
		r = -1;
	} else {
		modload("cpuctl");
		while ((direntry = readdir(dirp)) != NULL) {
			if (direntry->d_namlen == 0)
				continue;
			if (!strncmp( direntry->d_name, "cpuctl", 6)) {
				int x = atoi( direntry->d_name + 6);
				if (x > high)
					high = x;
		}	}
		r = closedir( dirp);
	}
 	r = (r) ? -1 : ++high;
	return r;
}


static int 
cpu_setHandler( void)
{
	unsigned int i;
	int          r = -1;
	
	for (i = 0; i < NHANDLERS; i++)
		if (handlers[ i]->probe( &cpupbuf) == 0) {
			r = i;
			break;
		}
	if (r >= 0 && i < NHANDLERS) {
		handler = handlers[ i];
		r = i;
	} else
		r = -1;
	return r;
}


static int 
isdir( const char *path)
{
	int r;
	struct stat st;
	
	r = stat( path, &st);
	if (r < 0)
		INFO( 0, "stat(%s) failed\n", path);
	return (r < 0) ? r : (st.st_mode & S_IFDIR);
}


#ifdef CPUCTL_EVAL_CPU_FEATURES
static int
do_eval_cpu_features( const char *dev)
{
	int fd, error;
	
	assert(dev != NULL);
	fd = open(dev, O_RDWR);
	if (fd < 0) {
		INFO(0, "register new CPU features: error opening %s for writing\n", dev);
		return ( 1);
	}
	error = ioctl( fd, CPUCTL_EVAL_CPU_FEATURES, NULL);
	if (error < 0)
		INFO(0, "Error with registering new CPU features on %s\n", dev);
	close( fd);
	return( error);
}
#endif


int 
main( int argc, char *argv[])
{
	int   c, cmd, r = 0;
	char *data;
	int   ambigc = 0;
	int   ambigv = 0;
	const char *prgname;

	if ((prgname = getprogname()) != NULL)
		pgmn = (char *) prgname;
	memset( &cpupbuf, 0, sizeof( struct cpupdate_params));
	
	if (argc == 1)
		usage();
	while ((c = getopt( argc, argv, "U:c:f:uihIqvwp:s:S:T:CXV")) != -1) {
		switch (c) {
			case 'U':
			case 'c': 
			case 'f': 	if (strlen( optarg) < MAXPATHLEN) {
		  					if (c == 'f' || c == 'U') {
								strcpy( (char *) &cpupbuf.filepath, optarg);
							} else if (c == 'c') {
								data = optarg;
							}
						} else {
							INFO( 0, "ERROR: Path too long\n");
							r = 1;
							break;
						}
			case 'C': 
			case 'X': 
			case 'V': 
			case 'u': 
			case 'i': 
			case 'h':	cmd = c;
						if (ambigc) {
							INFO( 0, "ERROR: no combination of the [UcfuihCX] options possible\n");
							r = 1;
							break;
						}
						ambigc = 1;
						break;
			case 'I':	vendormode = VENDOR_INDEX_INTEL;
						if (ambigv) {
							INFO( 0, "ERROR: only one vendor mode option allowed\n");
							r = 1;
							break;
						}
						ambigv = 1;
						break;
#if 0
			case 'A':	vendormode = VENDOR_INDEX_AMD;
						if (ambigv) {
							INFO( 0, "ERROR: only one vendor mode option allowed\n");
							r = 1;
							break;
						}
						ambigv = 1;
						break;
			case 'V':	vendormode = VENDOR_INDEX_VIA;
						if (ambigv) {
							INFO( 0, "ERROR: only one vendor mode option allowed\n");
							r = 1;
							break;
						}
						ambigv = 1;
						break;
#endif
			case 'q':	verbosity -= 10;
						break;
			case 'v':	++verbosity;
						break;
			case 'p': 	if (strlen( optarg) < MAXPATHLEN) {
							strcpy( (char *) &cpupbuf.primdir, optarg);
						} else {
							INFO( 0, "ERROR: primdir path name too long\n");
							r = 1;
						}
						break;
			case 's': 	if (strlen( optarg) < MAXPATHLEN) {
							strcpy( (char *) &cpupbuf.secdir, optarg);
						} else {
							INFO( 0, "ERROR: secdir path name too long\n");
							r = 1;
						}
						break;
			case 'S': 	if (strlen( optarg) < MAXPATHLEN) {
							strcpy( (char *) &cpupbuf.srcdir, optarg);
						} else {
							INFO( 0, "ERROR: source dir path name too long\n");
							r = 1;
						}
						break;
			case 'T': 	if (strlen( optarg) < MAXPATHLEN) {
							strcpy( (char *) &cpupbuf.targetdir, optarg);
						} else {
							INFO( 0, "ERROR: target dir path name too long\n");
							r = 1;
						}
						break;
			case 'w':	++cpupbuf.writeit;
						break;
			default:	usage();
						// NOTREACHED
		}
	}
	if (!r) switch (cmd) {
		case 'V':	INFO( 0, "%s Version %s\n", pgmn, CPUPDATE_VERSION);
					break;
		case 'i':	numCores = cpu_getCoreNum();
					if (numCores < 1) {
						INFO( 0, "Failed to determine number of cores. Did you do 'kldload cpuctl'?\n");
						r = 1;
						break;
					}
					r = cpu_setHandler();
					if (r < 0) {
						INFO(10, "Sorry! This CPU brand is unsupported.\n");
						break;
					}
					INFO( 10, "Found CPU(s) from %s\n", handler->getvendorname());
					r = handler->printcpustats( &cpupbuf);
					break;
		case 'c':
		case 'f': 	if (vendormode < 0) {
						INFO( 0, "ERROR: vendor mode option missing\n");
							r = 1;
							break;
					}
					handler = handlers[ vendormode];
					if (cmd == 'f') {
						handler->loadcheckmicrocode( &cpupbuf);
						handler->printmicrocodestats( &cpupbuf);
						break;
					} else if (cmd == 'c') {
						DIR *dirp;
						struct dirent *direntry;
						char fpath[ MAXPATHLEN];
						
						dirp = opendir( data);
						if (dirp == NULL) {
							INFO( 0, "Failed to access directory %s\n", data);
						} else {
							while ((direntry = readdir( dirp)) != NULL) {
								if (direntry->d_namlen == 0 ||
										strcmp( direntry->d_name, ".") == 0 ||
										strcmp( direntry->d_name, "..") == 0)
									continue;
								if (snprintf( fpath, sizeof( fpath), "%s/%s", data, direntry->d_name) >= (signed int) sizeof( fpath)) {
									INFO( 0, "skipping %s, filename buffer too short\n", direntry->d_name);
									continue;
								}
								if (isdir( fpath) != 0) {
									INFO( 0, "skipping %s: is a directory\n", fpath);
									continue;
								}
								if (strlen( data) < MAXPATHLEN) {
									strcpy( (char *) &cpupbuf.filepath, fpath);
									handler->loadcheckmicrocode( &cpupbuf);
									handler->freeucodeinfo( &cpupbuf);
//									handler->verifyUpdF( fpath, direntry->d_name);
								} else {
									INFO( 0, "ERROR: Path too long\n");
								}
							}
						}
					}
					break;
		case 'C':	// compact single-blobbed files to new multi-blobbed files or...
		case 'X':	// extract multi-blobbed (new format) microcode files to single-blobbed (old format) files
					if (vendormode < 0) {
						INFO( 0, "ERROR: vendor mode option missing\n");
						r = 1;
						break;
					}
					if (vendormode != VENDOR_INDEX_INTEL) {
						INFO( 0, "Sorry, this function currently only supports Intel multi-blobbed format\n");
						r = 1;
						break;
					}
					handler = handlers[ vendormode];
					// verify that source and target directories have been specified
					if (!strlen( cpupbuf.srcdir) || !strlen( cpupbuf.targetdir)) {
						INFO( 0, "Please specify both source and target directories!\n");
						r = 1;
						break;
					}
					// walk thru all files in source dir, load every file, and if valid, 
					// then write every blob contained to a files of ff-mm-ss-flags filename format
					DIR *dirp;
					struct dirent *direntry;
					char fpath[ MAXPATHLEN];
					
					dirp = opendir( cpupbuf.srcdir);
					if (dirp == NULL) {
						INFO( 0, "Failed to access directory %s\n", cpupbuf.srcdir);
					} else {
						while ((direntry = readdir( dirp)) != NULL) {
							if (direntry->d_namlen == 0 ||
									strcmp( direntry->d_name, ".") == 0 ||
									strcmp( direntry->d_name, "..") == 0)
								continue;
							if (snprintf( fpath, sizeof( fpath), "%s/%s", cpupbuf.srcdir, direntry->d_name) >= (signed int) sizeof( fpath)) {
								INFO( 0, "skipping %s, filename buffer too short\n", direntry->d_name);
								continue;
							}
							if (isdir( fpath) != 0) {
								INFO( 0, "skipping %s: is a directory\n", fpath);
								continue;
							}
							if (strlen( fpath) < MAXPATHLEN) {
								strcpy( (char *) &cpupbuf.filepath, fpath);
								r = handler->loadcheckmicrocode( &cpupbuf);
								if (r) {
									INFO( 0, "Error with microcode file %s, skipping that file\n", fpath);
								} else {
									if (cmd == 'X') {
										if (handler->extractformat( &cpupbuf)) {
											INFO( 0, "ERROR: Error while extracting microcode file %s\n", fpath);
											r = 1;
											break;
										}
									} else {
									  // (cmd == 'C')
										if (handler->compactformat( &cpupbuf)) {
											INFO( 0, "ERROR: Error while compacting microcode file %s\n", fpath);
											r = 1;
											break;
										}
									}
								}
								handler->freeucodeinfo( &cpupbuf);
							} else {
								INFO( 0, "ERROR: Path too long\n");
							}
						}
					}
					break;
		case 'U':	
		case 'u': 	numCores = cpu_getCoreNum();
					if (numCores < 1) {
						INFO( 0, "Failed to determine number of cores. Did you do 'kldload cpuctl'?\n");
						r = -1;
						break;
					} 
					r = cpu_setHandler();
					if (r < 0) {
						INFO(10, "Sorry! This CPU brand is unsupported.\n");
						break;
					}
					INFO( 10, "Found CPU(s) from %s\n", handler->getvendorname());
					if (cmd == 'u') {
						if (!strlen( cpupbuf.primdir))
							strcpy( cpupbuf.primdir, MICROCODE_REPO_PATH_PRIM);
						if (!strlen( cpupbuf.secdir))
							strcpy( cpupbuf.secdir, MICROCODE_REPO_PATH_SEC);
						strcat( cpupbuf.primdir, "/");
						strcat( cpupbuf.secdir, "/");
						strcat( cpupbuf.primdir, handler->getvendorname());
						strcat( cpupbuf.secdir, handler->getvendorname());
						r = handler->loadcheckmicrocode( &cpupbuf);
					} 
					if (!r)
						r = handler->update( &cpupbuf);
	// this #ifdef is for updating microcode on older FreeBSD versions
	// which do not have the CPUCTL_EVAL_CPU_FEATURES feature
#ifdef CPUCTL_EVAL_CPU_FEATURES
					if (!r) {
						INFO( 10, "No updating error. Registering CPU features\n");
						for ( int i = 0; i < numCores; ++i) {
							char cpupath[ MAXPATHLEN];
							sprintf( cpupath, "/dev/cpuctl%d", i);
							r = do_eval_cpu_features( cpupath);
							r = (r < 0) ? 1 : 0;   // error if negative
							if (r) {
								INFO( 0, "Failed to register core %d features\n", i);
								r = -1;
								break;
							}
						}
						if (!r)
							INFO( 10, "Successfully registered new CPU features\n");
						handler->freeucodeinfo( &cpupbuf);
					}
#else
					if (!r) {
						INFO( 10, "No updating error.\n");
						INFO( 10, "NOTICE: This FreeBSD version does not support registering new CPU features!\n");
						handler->freeucodeinfo( &cpupbuf);
					}
#endif
					if (!cpupbuf.writeit) {
						INFO( 10, "ATTENTION NOTICE: -w option missing! No actual update, only dry run done!.\n");
					}
					break;
		case 'h': 
		default :	usage();
					// NOTREACHED
	}
	return r;
}
