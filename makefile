CC=cc
LD=/usr/bin/ld

.if defined (DEBUG)
CFLAGS=		-c -O0 -pipe -g -std=gnu99 -fstack-protector-strong -Qunused-arguments
.else
CFLAGS=		-c -O2 -pipe -std=gnu99 -fstack-protector-strong -Qunused-arguments
.endif

LDFLAGS=	--eh-frame-hdr -dynamic-linker /libexec/ld-elf.so.1 --hash-style=both \
		--enable-new-dtags -o $(TARGET) /usr/lib/crt1.o /usr/lib/crti.o \
		/usr/lib/crtbegin.o -L/usr/lib $(OBJS) -lgcc --as-needed \
		-lgcc_s --no-as-needed -lc -lgcc --as-needed -lgcc_s --no-as-needed \
		/usr/lib/crtend.o /usr/lib/crtn.o

TARGET=		cpupdate
	               
OBJS=		cpupdate.o intel.o

$(TARGET) :     $(OBJS)
		$(LD) $(LDFLAGS)
	
cpupdate.o :    cpupdate.c intel.h cpupdate.h
		$(CC) $(CFLAGS) cpupdate.c 
intel.o	:       intel.c intel.h cpupdate.h
		$(CC) $(CFLAGS) intel.c

build:          $(TARGET)

clean:          
		rm $(OBJS) $(TARGET)

