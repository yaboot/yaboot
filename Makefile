## Configuration section

VERSION = 1.3
# Debug mode (verbose)
DEBUG = 0
ROOT =
PREFIX = usr/local
MANDIR = man
GETROOT = fakeroot

# Enable text colors
CONFIG_COLOR_TEXT = y
# Enable colormap setup
CONFIG_SET_COLORMAP = y
# Enable splash screen
CONFIG_SPLASH_SCREEN = n
# Enable md5 passwords
USE_MD5_PASSWORDS = y

# We use fixed addresses to avoid overlap when relocating
# and other trouble with initrd

# Load the bootstrap at 2Mb
TEXTADDR	= 0x200000
# Malloc block at 3Mb -> 4Mb
MALLOCADDR	= 0x300000
MALLOCSIZE	= 0x100000
# Load kernel at 20Mb and ramdisk just after
KERNELADDR	= 0x01400000

# Set this to the prefix of your cross-compiler, if you have one.
# Else leave it empty.
#
CROSS = 

# The flags for the target compiler.
#
CFLAGS = -Os -nostdinc -Wall -isystem `gcc -print-file-name=include` -fsigned-char
CFLAGS += -DVERSION=\"${VERSION}\"	#"
CFLAGS += -DTEXTADDR=$(TEXTADDR) -DDEBUG=$(DEBUG)
CFLAGS += -DMALLOCADDR=$(MALLOCADDR) -DMALLOCSIZE=$(MALLOCSIZE)
CFLAGS += -DKERNELADDR=$(KERNELADDR)
CFLAGS += -I ./include

ifeq ($(CONFIG_COLOR_TEXT),y)
CFLAGS += -DCONFIG_COLOR_TEXT
endif

ifeq ($(CONFIG_SET_COLORMAP),y)
CFLAGS += -DCONFIG_SET_COLORMAP
endif

ifeq ($(CONFIG_SPLASH_SCREEN),y)
CFLAGS += -DCONFIG_SPLASH_SCREEN
endif

ifeq ($(USE_MD5_PASSWORDS),y)
CFLAGS += -DUSE_MD5_PASSWORDS
endif

# Link flags
#
LFLAGS = -Ttext $(TEXTADDR) -Bstatic 

# Libraries
#
LLIBS = lib/libext2fs.a
#LLIBS = -l ext2fs

# For compiling build-tools that run on the host.
#
HOSTCC = gcc
HOSTCFLAGS = -I/usr/include $(CFLAGS)

## End of configuration section

OBJS = second/crt0.o second/yaboot.o second/cache.o second/prom.o second/file.o \
	second/partition.o second/fs.o second/cfg.o second/setjmp.o second/cmdline.o \
	second/fs_of.o second/fs_ext2.o second/fs_reiserfs.o second/fs_iso.o second/iso_util.o \
	lib/nosys.o lib/string.o lib/strtol.o lib/vsprintf.o lib/ctype.o lib/malloc.o lib/strstr.o

ifeq ($(CONFIG_SPLASH_SCREEN),y)
OBJS += second/gui/effects.o second/gui/colormap.o second/gui/video.o second/gui/pcx.o
endif

ifeq ($(USE_MD5_PASSWORDS),y)
OBJS += second/md5.o
endif

CC = $(CROSS)gcc
LD = $(CROSS)ld
AS = $(CROSS)as
OBJCOPY = $(CROSS)objcopy

all: yaboot addnote mkofboot

lgcc = `$(CC) -print-libgcc-file-name`

yaboot: $(OBJS)
	$(LD) $(LFLAGS) $(OBJS) $(LLIBS) $(lgcc) -o second/$@
	chmod -x second/yaboot

addnote:
	$(HOSTCC) $(HOSTCFLAGS) -o util/addnote util/addnote.c

elfextract:
	$(HOSTCC) $(HOSTCFLAGS) -o util/elfextract util/elfextract.c

mkofboot:
	ln -sf ybin ybin/mkofboot

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -D__ASSEMBLY__  -c -o $@ $<

dep:
	makedepend -Iinclude *.c lib/*.c util/*.c gui/*.c

bindist: all
	mkdir ../yaboot-binary-${VERSION}
	${GETROOT} make ROOT=../yaboot-binary-${VERSION} install
	mkdir -p -m 755 ../yaboot-binary-${VERSION}/usr/local/share/doc/yaboot
	cp -a COPYING ../yaboot-binary-${VERSION}/usr/local/share/doc/yaboot/COPYING
	cp -a README ../yaboot-binary-${VERSION}/usr/local/share/doc/yaboot/README
	${GETROOT} tar -C ../yaboot-binary-${VERSION} -zcvpf ../yaboot-binary-${VERSION}.tar.gz .
	rm -rf ../yaboot-binary-${VERSION}

clean:
	rm -f second/yaboot util/addnote util/elfextract $(OBJS)
	find . -name '#*' | xargs rm -f
	find . -name '.#*' | xargs rm -f
	find . -name '*~' | xargs rm -f
	rm -rf man.deb
	chmod 755 ybin/ybin ybin/ofpath ybin/yabootconfig
	chmod -R u+rwX,go=rX .
	chmod a-w COPYING

install: all
	@strip second/yaboot
	@strip --remove-section=.comment second/yaboot
	@strip util/addnote
	@strip --remove-section=.comment --remove-section=.note util/addnote
	install -d -o root -g root -m 0755 ${ROOT}/etc/
	install -d -o root -g root -m 0755 ${ROOT}/${PREFIX}/sbin/
	install -d -o root -g root -m 0755 ${ROOT}/${PREFIX}/lib
	install -d -o root -g root -m 0755 ${ROOT}/${PREFIX}/lib/yaboot
	install -d -o root -g root -m 0755 ${ROOT}/${PREFIX}/${MANDIR}/man5/
	install -d -o root -g root -m 0755 ${ROOT}/${PREFIX}/${MANDIR}/man8/
	install -o root -g root -m 0644 second/yaboot ${ROOT}/$(PREFIX)/lib/yaboot
	install -o root -g root -m 0755 util/addnote ${ROOT}/${PREFIX}/lib/yaboot/addnote
	install -o root -g root -m 0644 first/ofboot ${ROOT}/${PREFIX}/lib/yaboot/ofboot
	install -o root -g root -m 0755 ybin/ofpath ${ROOT}/${PREFIX}/sbin/ofpath
	install -o root -g root -m 0755 ybin/ybin ${ROOT}/${PREFIX}/sbin/ybin
	install -o root -g root -m 0755 ybin/yabootconfig ${ROOT}/${PREFIX}/sbin/yabootconfig
	rm -f ${ROOT}/${PREFIX}/sbin/mkofboot
	ln -s ybin ${ROOT}/${PREFIX}/sbin/mkofboot
	@gzip -9 man/*.[58]
	install -o root -g root -m 0644 man/bootstrap.8.gz ${ROOT}/${PREFIX}/${MANDIR}/man8/bootstrap.8.gz
	install -o root -g root -m 0644 man/mkofboot.8.gz ${ROOT}/${PREFIX}/${MANDIR}/man8/mkofboot.8.gz
	install -o root -g root -m 0644 man/ofpath.8.gz ${ROOT}/${PREFIX}/${MANDIR}/man8/ofpath.8.gz
	install -o root -g root -m 0644 man/yaboot.8.gz ${ROOT}/${PREFIX}/${MANDIR}/man8/yaboot.8.gz
	install -o root -g root -m 0644 man/yabootconfig.8.gz ${ROOT}/${PREFIX}/${MANDIR}/man8/yabootconfig.8.gz
	install -o root -g root -m 0644 man/ybin.8.gz ${ROOT}/${PREFIX}/${MANDIR}/man8/ybin.8.gz
	install -o root -g root -m 0644 man/yaboot.conf.5.gz ${ROOT}/${PREFIX}/${MANDIR}/man5/yaboot.conf.5.gz
	@gunzip man/*.gz
	@[ ! -e ${ROOT}/etc/yaboot.conf ] && install -o root -g root -m 0644 etc/yaboot.conf ${ROOT}/etc/yaboot.conf
	@echo
	@echo "Installation successful."
	@echo
	@echo "An example /etc/yaboot.conf has been installed (unless /etc/yaboot.conf already existed"
	@echo "You may either alter that file to match your system, or alternatively run yabootconfig"
	@echo "yabootconfig will generate a simple and valid /etc/yaboot.conf for your system"
	@echo

deinstall:
	rm -f ${ROOT}/${PREFIX}/sbin/ofpath
	rm -f ${ROOT}/${PREFIX}/sbin/ybin
	rm -f ${ROOT}/${PREFIX}/sbin/yabootconfig
	rm -f ${ROOT}/${PREFIX}/sbin/mkofboot
	rm -f ${ROOT}/${PREFIX}/lib/yaboot/yaboot
	rm -f ${ROOT}/${PREFIX}/lib/yaboot/ofboot
	rm -f ${ROOT}/${PREFIX}/lib/yaboot/addnote
	@rmdir ${ROOT}/${PREFIX}/lib/yaboot || true
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man8/bootstrap.8.gz
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man8/mkofboot.8.gz
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man8/ofpath.8.gz
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man8/yaboot.8.gz
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man8/yabootconfig.8.gz
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man8/ybin.8.gz
	rm -f ${ROOT}/${PREFIX}/${MANDIR}/man5/yaboot.conf.5.gz
	@if [ -L ${ROOT}/boot/yaboot -a ! -e ${ROOT}/boot/yaboot ] ; then rm -f ${ROOT}/boot/yaboot ; fi
	@if [ -L ${ROOT}/boot/ofboot.b -a ! -e ${ROOT}/boot/ofboot.b ] ; then rm -f ${ROOT}/boot/ofboot.b ; fi
	@echo
	@echo "Deinstall successful."
	@echo "${ROOT}/etc/yaboot.conf has not been removed, you may remove it yourself if you wish."

uninstall: deinstall
