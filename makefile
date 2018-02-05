####################################################################
# Paths Start
####################################################################
WORKDIR=/home/stefan/code/cpupdate
####################################################################
# Paths End
####################################################################
# DEBUG=1

CC=cc
AR=ar
LD="/usr/bin/ld"

#.if defined $(DEBUG)
CFLAGS=    -c -O0 -pipe -g -std=gnu99 -fstack-protector-strong -Qunused-arguments
LDFLAGS=   --eh-frame-hdr -dynamic-linker /libexec/ld-elf.so.1 --hash-style=both \
           --enable-new-dtags -o $(TARGET).full /usr/lib/crt1.o /usr/lib/crti.o \
           /usr/lib/crtbegin.o -L/usr/lib $(OBJS) -lgcc --as-needed \
           -lgcc_s --no-as-needed -lc -lgcc --as-needed -lgcc_s --no-as-needed \
           /usr/lib/crtend.o /usr/lib/crtn.o
# .else
# CFLAGS= -O2 -pipe -std=gnu99 -fstack-protector-strong -Qunused-arguments
# LDFLAGS= 
# 
# .endif

####################################################################
# Runtime-Library Start
####################################################################
####################################################################
# Runtime-Library End
####################################################################


####################################################################
# Linker Stuff Start
####################################################################
####################################################################
# Linker Stuff End
####################################################################

####################################################################
# 
####################################################################
TARGET=         cpupdate
	               
OBJS=     cpupdate.o intel.o

$(TARGET) :     $(OBJS)
	                 $(LD) $(LDFLAGS)
	
cpupdate.o :    cpupdate.c intel.h cpucommon.h cpupdate.h
			$(CC) $(CFLAGS) cpupdate.c 
intel.o	:       intel.c intel.h cpucommon.h cpupdate.h
			$(CC) $(CFLAGS) intel.c

build:          $(TARGET)
	
clean:          
			rm $(MODULEOBJS) cpupdate.full cpupdate

