/* Yaboot - secondary boot loader for Linux on ppc.

   Copyright (C) 1999 Benjamin Herrenschmidt

   portions based on poof

   Copyright (C) 1999 Marius Vollmer

   portions based on quik
   
   Copyright (C) 1996 Paul Mackerras.

   Because this program is derived from the corresponding file in the
   silo-0.64 distribution, it is also
sys
   Copyright (C) 1996 Pete A. Zaitcev
   		 1996 Maurizio Plaza
   		 1996 David S. Miller
   		 1996 Miguel de Icaza
   		 1996 Jakub Jelinek

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "stdarg.h"
#include "string.h"
#include "ctype.h"
#include "stdlib.h"
#include "prom.h"
#include "file.h"
#include "cfg.h"
#include "cmdline.h"
#include "yaboot.h"
#include "linux/elf.h"
#include "bootinfo.h"

#define CONFIG_FILE_NAME	"yaboot.conf"
#define CONFIG_FILE_MAX		0x8000		/* 32k */

#ifdef CONFIG_SPLASH_SCREEN
#include "gui.h"
#endif /* CONFIG_SPLASH_SCREEN */

#ifdef USE_MD5_PASSWORDS
#include "md5.h"
#endif /* USE_MD5_PASSWORDS */

/* align addr on a size boundry - adjust address up if needed -- Cort */
#define _ALIGN(addr,size)	(((addr)+size-1)&(~(size-1)))

/* Addresses where the PPC32 and PPC64 vmlinux kernels are linked at.
 * These are used to determine whether we are booting a vmlinux, in
 * which case, it will be loaded at KERNELADDR.  Otherwise (eg zImage),
 * we load the binary where it was linked at (ie, e_entry field in
 * the ELF header).
 */
#define KERNEL_LINK_ADDR_PPC32	0xC0000000UL
#define KERNEL_LINK_ADDR_PPC64	0xC000000000000000ULL

typedef struct {
    union {
        Elf32_Ehdr  elf32hdr;
        Elf64_Ehdr  elf64hdr;
    } elf;
    void*	    base;
    unsigned long   memsize;
    unsigned long   filesize;
    unsigned long   offset;
    unsigned long   load_loc;
    unsigned long   entry;
} loadinfo_t;

typedef void (*kernel_entry_t)( void *,
                                unsigned long,
                                prom_entry,
                                unsigned long,
                                unsigned long );

/* Imported functions */
extern unsigned long reloc_offset(void);
extern long flush_icache_range(unsigned long start, unsigned long stop);

/* Exported functions */
int	yaboot_start(unsigned long r3, unsigned long r4, unsigned long r5);

/* Local functions */
static int	yaboot_main(void);
static int	is_elf32(loadinfo_t *loadinfo);
static int	is_elf64(loadinfo_t *loadinfo);
static int      load_elf32(struct boot_file_t *file, loadinfo_t *loadinfo);
static int      load_elf64(struct boot_file_t *file, loadinfo_t *loadinfo);
static void     setup_display(void);

/* Locals & globals */

int useconf = 0;
char bootdevice[1024];
char *bootpath = NULL;
char *password = NULL;
int bootpartition = -1;
int _machine = _MACH_Pmac;

#ifdef CONFIG_COLOR_TEXT

/* Color values for text ui */
static struct ansi_color_t {
	char*	name;
	int	index;
	int	value;
} ansi_color_table[] = {
	{ "black",		2, 30 },
	{ "blue",		0, 31 },
	{ "green",		0, 32 },
	{ "cyan",		0, 33 },
	{ "red",		0, 34 },
	{ "purple",		0, 35 },
	{ "brown",		0, 36 },
	{ "light-gray", 	0, 37 },
	{ "dark-gray",		1, 30 },
	{ "light-blue",		1, 31 },
	{ "light-green",	1, 32 },
	{ "light-cyan",		1, 33 },
	{ "light-red",		1, 34 },
	{ "light-purple",	1, 35 },
	{ "yellow",		1, 36 },
	{ "white",		1, 37 },
	{ NULL,			0, 0 },
};

/* Default colors for text ui */
int fgcolor = 15;
int bgcolor = 0;
#endif /* CONFIG_COLOR_TEXT */

#if DEBUG
static int test_bss;
static int test_data = 0;
#endif
static int pause_after;
static char *pause_message = "Type go<return> to continue.\n";
static char given_bootargs[1024];
static int given_bootargs_by_user = 0;

extern unsigned char linux_logo_red[];
extern unsigned char linux_logo_green[];
extern unsigned char linux_logo_blue[];

#define DEFAULT_TIMEOUT		-1

/* Entry, currently called directly by crt0 (bss not inited) */

extern char* __bss_start;
extern char* _end;

static struct first_info *quik_fip = NULL;

int
yaboot_start (unsigned long r3, unsigned long r4, unsigned long r5)
{
	int result;
	void* malloc_base = NULL;
	prom_handle root;

 	/* OF seems to do it, but I'm not very confident */
  	memset(&__bss_start, 0, &_end - &__bss_start);
  	
	/* Check for quik first stage bootloader (but I don't think we are
	 * compatible with it anyway, I'll look into backporting to older OF
	 * versions later
	 */
	if (r5 == 0xdeadbeef) {
		r5 = r3;
		quik_fip = (struct first_info *)r4;
	}

  	/* Initialize OF interface */
	prom_init ((prom_entry) r5);
	
	/* Allocate some memory for malloc'ator */
	malloc_base = prom_claim((void *)MALLOCADDR, MALLOCSIZE, 0);
	if (malloc_base == (void *)-1) {
		prom_printf("Can't claim malloc buffer (%d bytes at 0x%08lx)\n",
			MALLOCSIZE, MALLOCADDR);
		return -1;
	}
	malloc_init(malloc_base, MALLOCSIZE);
#if DEBUG
	prom_printf("Malloc buffer allocated at 0x%x (%d bytes)\n",
		malloc_base, MALLOCSIZE);
#endif
		
	/* A few useless printf's */
#if DEBUG
	prom_printf("reloc_offset :  %ld         (should be 0)\n", reloc_offset());
	prom_printf("test_bss     :  %d         (should be 0)\n", test_bss);
	prom_printf("test_data    :  %d         (should be 0)\n", test_data);
	prom_printf("&test_data   :  0x%08lx\n", &test_data);
	prom_printf("&test_bss    :  0x%08lx\n", &test_bss);
	prom_printf("linked at    :  0x%08lx\n", TEXTADDR);
#endif	
	/* ask the OF info if we're a chrp or pmac */
	/* we need to set _machine before calling finish_device_tree */
	root = prom_finddevice("/");
	if (root != 0) {
		static char model[256];
		if (prom_getprop(root, "device_type", model, 256 ) > 0 &&
			!strncmp("chrp", model, 4))
			_machine = _MACH_chrp;
		else {
			if (prom_getprop(root, "model", model, 256 ) > 0 &&
				!strncmp(model, "IBM", 3))
				_machine = _MACH_chrp;
		}
	}
	
#if DEBUG
	prom_printf("Running on _machine = %d\n", _machine);
#endif

	/* Call out main */
	result = yaboot_main();

	/* Get rid of malloc pool */
	malloc_dispose();
	prom_release(malloc_base, MALLOCSIZE);
#if DEBUG
	prom_printf("Malloc buffer released. Exiting with code %d\n",
		result);

#endif

	/* Return to OF */
	prom_exit();
	
	return result;
	
}

#ifdef CONFIG_COLOR_TEXT
/*
 * Validify color for text ui
 */
static int
check_color_text_ui(char *color)
{
	int i = 0;
	while(ansi_color_table[i].name) {
		if (!strcmp(color, ansi_color_table[i].name))
			return i;
		i++;
	}
	return -1;
}      
#endif /* CONFIG_COLOR_TEXT */

/* Currently, the config file must be at the root of the filesystem.
 * todo: recognize the full path to myself and use it to load the
 * config file. Handle the "\\" (blessed system folder)
 */
static int
load_config_file(char *device, char* path, int partition)
{
    char *conf_file = NULL, *p;
    struct boot_file_t file;
    int sz, opened = 0, result = 0;
    char conf_path[512];
    struct boot_fspec_t fspec;

    /* Allocate a buffer for the config file */
    conf_file = malloc(CONFIG_FILE_MAX);
    if (!conf_file) {
    	prom_printf("Can't alloc config file buffer\n");
    	goto bail;
    }

    /* Build the path to the file */
    if (path)
    	strcpy(conf_path, path);
    else if ( _machine == _MACH_chrp )
    	strcpy(conf_path, "/etc/");
    else
    	conf_path[0] = 0;
    strcat(conf_path, CONFIG_FILE_NAME);

    /* Open it */
    fspec.dev = device;
    fspec.file = conf_path;
    fspec.part = partition;
    result = open_file(&fspec, &file);
    if (result != FILE_ERR_OK) {
    	prom_printf("Can't open config file '%s', err: %d\n", conf_path, result);
	goto bail;
    }
    opened = 1;

    /* Read it */
    sz = file.fs->read(&file, CONFIG_FILE_MAX, conf_file);
    if (sz <= 0) {
    	prom_printf("Error, can't read config file\n");
    	goto bail;
    }
    prom_printf("Config file read, %d bytes\n", sz);

    /* Close the file */
    if (opened)
    	file.fs->close(&file);
    opened = 0;

    /* Call the parsing code in cfg.c */
    if (cfg_parse(conf_path, conf_file, sz) < 0) {
	prom_printf ("Syntax error or read error config\n");
	goto bail;
    }

#if DEBUG
    prom_printf("Config file successfully parsed\n", sz);
#endif

    /* Now, we do the initialisations stored in the config file */
    p = cfg_get_strg(0, "init-code");
    if (p)
	prom_interpret(p);

    password = cfg_get_strg(0, "password");
	
#ifdef CONFIG_COLOR_TEXT
    p = cfg_get_strg(0, "fgcolor");
    if (p) {
#if DEBUG       
       prom_printf("fgcolor=%s\n", p);
#endif
       fgcolor = check_color_text_ui(p);
       if (fgcolor == -1) {
	  prom_printf("Invalid fgcolor: \"%s\".\n", p);
       }
    }
    p = cfg_get_strg(0, "bgcolor");
    if (p) {
#if DEBUG      
       prom_printf("bgcolor=%s\n", p);
#endif
       bgcolor = check_color_text_ui(p);
       if (bgcolor == -1)
	  prom_printf("Invalid bgcolor: \"%s\".\n", p);
    }
    if (bgcolor >= 0) {
       	char temp[64];
       	sprintf(temp, "%x to background-color", bgcolor); 
	prom_interpret(temp); 
#if !DEBUG
	prom_printf("\xc");
#endif	
    }
    if (fgcolor >= 0) {
       	char temp[64];
       	sprintf(temp, "%x to foreground-color", fgcolor); 
	prom_interpret(temp); 
    }
#endif /* CONFIG_COLOR_TEXT */
   
    p = cfg_get_strg(0, "init-message");
    if (p)
	prom_printf("%s\n", p);
#if 0
    p = cfg_get_strg(0, "message");
    if (p)
	print_message_file(p);
#endif		

    result = 1;
    
bail:

    if (opened)
    	file.fs->close(&file);
    
    if (result != 1 && conf_file)
    	free(conf_file);
    	
    return result;
}

void maintabfunc (void)
{
    if (useconf) {
	cfg_print_images();
	prom_printf("boot: %s", cbuff);
    }
}

void
word_split(char **linep, char **paramsp)
{
    char *p;

    *paramsp = 0;
    p = *linep;
    if (p == 0)
	return;
    while (*p == ' ')
	++p;
    if (*p == 0) {
	*linep = 0;
	return;
    }
    *linep = p;
    while (*p != 0 && *p != ' ')
	++p;
    while (*p == ' ')
	*p++ = 0;
    if (*p != 0)
	*paramsp = p;
}

char *
make_params(char *label, char *params)
{
    char *p, *q;
    static char buffer[2048];

    q = buffer;
    *q = 0;

    p = cfg_get_strg(label, "literal");
    if (p) {
	strcpy(q, p);
	q = strchr(q, 0);
	if (params) {
	    if (*p)
		*q++ = ' ';
	    strcpy(q, params);
	}
	return buffer;
    }

    p = cfg_get_strg(label, "root");
    if (p) {
	strcpy (q, "root=");
	strcpy (q + 5, p);
	q = strchr (q, 0);
	*q++ = ' ';
    }
    if (cfg_get_flag(label, "read-only")) {
	strcpy (q, "ro ");
	q += 3;
    }
    if (cfg_get_flag(label, "read-write")) {
	strcpy (q, "rw ");
	q += 3;
    }
    p = cfg_get_strg(label, "ramdisk");
    if (p) {
	strcpy (q, "ramdisk=");
	strcpy (q + 8, p);
	q = strchr (q, 0);
	*q++ = ' ';
    }
    p = cfg_get_strg(label, "initrd-size");
    if (p) {
	strcpy (q, "ramdisk_size=");
	strcpy (q + 13, p);
	q = strchr (q, 0);
	*q++ = ' ';
    }
    if (cfg_get_flag(label, "novideo")) {
	strcpy (q, "video=ofonly");
	q = strchr (q, 0);
	*q++ = ' ';
    }
    p = cfg_get_strg (label, "append");
    if (p) {
	strcpy (q, p);
	q = strchr (q, 0);
	*q++ = ' ';
    }
    *q = 0;
    pause_after = cfg_get_flag (label, "pause-after");
    p = cfg_get_strg(label, "pause-message");
    if (p)
	pause_message = p;
    if (params)
	strcpy(q, params);

    return buffer;
}

void check_password(char *str)
{
    int i, end;

    for (i = 0; i < 3; i++) {
	 prom_printf ("\n%sassword: ", str);
	 passwdbuff[0] = 0;
	 cmdedit ((void (*)(void)) 0, 1);
	 prom_printf ("\n");
#ifdef USE_MD5_PASSWORDS
	 if (!strncmp (password, "$1$", 3)) {
	    if (!check_md5_password(passwdbuff, password))
	       return;
	 } 
	 else if (!strcmp (password, passwdbuff))
	    return;
#else
	 if (!strcmp (password, passwdbuff))
	    return;
#endif
	 if (i < 2)
	    prom_printf ("Password incorrect. Please try again...");
    }
    prom_printf ("Seems like you don't know the access password.  Go away!\n");
    end = (prom_getms() + 3000);
    while (prom_getms() <= end);
    prom_interpret("reset-all");
}

int get_params(struct boot_param_t* params)
{
    int defpart;
    char *defdevice = 0;
    char *p, *q, *endp;
    int c, n;
    char *imagename = 0, *label;
    int timeout = -1;
    int beg = 0, end;
    int singlekey = 0;
    int restricted = 0;
    static int first = 1;
    static char bootargs[1024];
    static char imagepath[1024];
    static char initrdpath[1024];
    static char sysmappath[1024];
#ifdef CONFIG_SPLASH_SCREEN
    static char splashpath[1024];
#endif /* CONFIG_SPLASH_SCREEN */

    pause_after = 0;
    memset(params, 0, sizeof(*params));
    params->args = "";
    params->kernel.part = -1;
    params->rd.part = -1;
    params->sysmap.part = -1;
    params->splash.part = -1;
    defpart = bootpartition;
    
    cmdinit();

    if (first) {
	first = 0;
	prom_get_chosen("bootargs", bootargs, sizeof(bootargs));
	imagename = bootargs;
	word_split(&imagename, &params->args);
	timeout = DEFAULT_TIMEOUT;
	if (imagename) {
	     prom_printf("Default supplied on the command line: ");
	     prom_printf("%s ", imagename);
	     if (params->args)
		  prom_printf("%s", params->args);
	     prom_printf("\n");
	}
	if (useconf && (q = cfg_get_strg(0, "timeout")) != 0 && *q != 0)
	    timeout = simple_strtol(q, NULL, 0);
    }

    prom_printf("boot: ");
    c = -1;
    if (timeout != -1) {
	 beg = prom_getms();
	 if (timeout > 0) {
	      end = beg + 100 * timeout;
	      do {
		   c = prom_nbgetchar();
	      } while (c == -1 && prom_getms() <= end);
	 }
	 if (c == -1)
	      c = '\n';
	 else if (c != '\n' && c != '\t' && c != '\r' && c != '\b' ) {
	      cbuff[0] = c;
	      cbuff[1] = 0;
	 }
    }

    if (c != -1 && c != '\n' && c != '\r') {
	 if (c == '\t') {
	      maintabfunc ();
	 }  else if (c >= ' ') {
	      cbuff[0] = c;
	      cbuff[1] = 0;
	      if ((cfg_get_flag (cbuff, "single-key")) && useconf) {
		   imagename = cbuff;
		   singlekey = 1;
		   prom_printf("%s\n", cbuff);
	      }
	 }
    }

    if (c == '\n' || c == '\r') {
         if (!imagename)
              imagename = cfg_get_default();
	 if (imagename)
	      prom_printf("%s", imagename);
	 if (params->args)
	      prom_printf(" %s", params->args);
	 prom_printf("\n");
    } else if (!singlekey) {
	 cmdedit(maintabfunc, 0);
	 prom_printf("\n");
	 strcpy(given_bootargs, cbuff);
	 given_bootargs_by_user = 1;
	 imagename = cbuff;
	 word_split(&imagename, &params->args);
    }

    /* chrp gets this wrong, force it -- Cort */
    if ( useconf && (!imagename || imagename[0] == 0 ))
	imagename = cfg_get_default();

    label = 0;
    defdevice = bootdevice;

    if (useconf) {
	defdevice = cfg_get_strg(0, "device");
	p = cfg_get_strg(0, "partition");
	if (p) {
	    n = simple_strtol(p, &endp, 10);
	    if (endp != p && *endp == 0)
		defpart = n;
	}
	p = cfg_get_strg(0, "pause-message");
	if (p)
	    pause_message = p;
	if (cfg_get_flag(0, "restricted"))
	    restricted = 1;
	p = cfg_get_strg(imagename, "image");
	if (p && *p) {
	    label = imagename;
	    imagename = p;
	    defdevice = cfg_get_strg(label, "device");
	    if(!defdevice) defdevice=bootdevice;
	    p = cfg_get_strg(label, "partition");
	    if (p) {
		n = simple_strtol(p, &endp, 10);
		if (endp != p && *endp == 0)
		    defpart = n;
	    }
	    if (cfg_get_flag(label, "restricted"))
		 restricted = 1;
	    if (label) {
		 if (params->args && password && restricted)
		      check_password ("To specify image arguments you must enter the p");
		 else if (password && !restricted)
		      check_password ("P");
	    }
	    params->args = make_params(label, params->args);
	}
    }

    if (!strcmp (imagename, "halt")) {
	 if (password)
	      check_password ("P");
	 prom_pause();
	 return 0;
    }
    if (!strcmp (imagename, "bye")) {
	 if (password) {
	      check_password ("P");
	      return 1;
	 }
	 return 1; 
    }

    if (imagename[0] == '$') {
	/* forth command string */
	 if (password)
	      check_password ("P");
	 prom_interpret(imagename+1);
	 return 0;
    }

    strncpy(imagepath, imagename, 1024);

    if (!label && password)
	 check_password ("To boot a custom image you must enter the p");

    params->kernel.dev = parse_device_path(imagepath, &params->kernel.file,
    	&params->kernel.part);
    if (validate_fspec(&params->kernel, defdevice, defpart) != FILE_ERR_OK) {
	prom_printf(
"Enter the kernel image name as [device:][partno]/path, where partno is a\n"
"number from 0 to 16.  Instead of /path you can type [mm-nn] to specify a\n"
"range of disk blocks (512B)\n"
"Example: hd:3,/vmlinux\n");
	return 0;
    }

    if (useconf) {
 	p = cfg_get_strg(label, "initrd");
	if (p && *p) {
#if DEBUG
	    prom_printf("parsing initrd path <%s>\n", p);
#endif	    
	    strncpy(initrdpath, p, 1024);
	    params->rd.dev = parse_device_path(initrdpath,
	    	&params->rd.file, &params->rd.part);
	    validate_fspec(&params->rd, defdevice, defpart);
	}
 	p = cfg_get_strg(label, "sysmap");
	if (p && *p) {
#if DEBUG
	    prom_printf("parsing sysmap path <%s>\n", p);
#endif	    
	    strncpy(sysmappath, p, 1024);
	    params->sysmap.dev = parse_device_path(sysmappath,
	    	&params->sysmap.file, &params->sysmap.part);
	    validate_fspec(&params->sysmap, defdevice, defpart);
	}
#ifdef CONFIG_SPLASH_SCREEN
 	p = cfg_get_strg(label, "splash");
	if (p && *p) {
#if DEBUG
	    prom_printf("parsing splash path <%s>\n", p);
#endif	    
	    strncpy(splashpath, p, 1024);
	    params->splash.dev = parse_device_path(splashpath,
	    	&params->splash.file, &params->splash.part);
	    validate_fspec(&params->splash, defdevice, defpart);
	}
#endif /* CONFIG_SPLASH_SCREEN */
   }
    
    return 0;
}

/* This is derived from quik core. To be changed to first parse the headers
 * doing lazy-loading, and then claim the memory before loading the kernel
 * to it
 * We also need to add initrd support to this whole mecanism
 */
void
yaboot_text_ui(void)
{
#define MAX_HEADERS	32

    struct boot_file_t	file;
    int			result;
    static struct boot_param_t	params;
    void		*initrd_base;
    unsigned long	initrd_size;
    void                *sysmap_base;
    unsigned long	sysmap_size;
    kernel_entry_t      kernel_entry;
    struct bi_record*	birec;
    char*               loc=NULL;
    loadinfo_t          loadinfo;
    void                *initrd_more,*initrd_want;
    unsigned long       initrd_read;
    
    loadinfo.load_loc = 0;

    for (;;) {
    	initrd_size = 0;
    	initrd_base = 0;
    	sysmap_base = 0;
    	sysmap_size = 0;
    	
	if (get_params(&params))
	    return;
	if (!params.kernel.file)
	    continue;
	
#ifdef CONFIG_SPLASH_SCREEN
	if (params.splash.file)
        	fxDisplaySplash(&params.splash);
#endif /* CONFIG_SPLASH_SCREEN */
	prom_printf("Please wait, loading kernel...\n");

	if(bootpath && !strcmp(bootpath,"\\\\") && params.kernel.file[0] != '/') {
		loc=(char*)malloc(strlen(params.kernel.file)+3);
		if (!loc) {
			prom_printf ("malloc error\n");
			goto next;
		}
		strcpy(loc,bootpath);
		strcat(loc,params.kernel.file);
		free(params.kernel.file);
		params.kernel.file=loc;
	}
        result = open_file(&params.kernel, &file);
        if (result != FILE_ERR_OK) {
	    prom_printf("\nImage not found.... try again\n");
	    goto next;
	}

	/* Read the Elf e_ident, e_type and e_machine fields to
	 * determine Elf file type
	 */
        if (file.fs->read(&file, sizeof(Elf_Ident), &loadinfo.elf) < sizeof(Elf_Ident)) {
	    prom_printf("\nCan't read Elf e_ident/e_type/e_machine info\n");
	    goto next;
	}

	if ( is_elf32(&loadinfo) ) {
            if ( !load_elf32(&file, &loadinfo) )
                goto next;
            prom_printf("   Elf32 kernel loaded...\n");
	} else if ( is_elf64(&loadinfo) ) {
            if ( !load_elf64(&file, &loadinfo) )
                goto next;
            prom_printf("   Elf64 kernel loaded...\n");
	} else {
            prom_printf ("Not a valid ELF image\n");
            goto next;
	}
        file.fs->close(&file);

	/* If sysmap, load it. 
	 */
	if (params.sysmap.file) {
	    prom_printf("Loading System.map ...\n");
	    if(bootpath && !strcmp(bootpath,"\\\\") && params.sysmap.file[0] != '/') {
		    if (loc) free(loc);
		    loc=(char*)malloc(strlen(params.sysmap.file)+3);
		    if (!loc) {
			    prom_printf ("malloc error\n");
			    goto next;
		    }
		    strcpy(loc,bootpath);
		    strcat(loc,params.sysmap.file);
		    free(params.sysmap.file);
		    params.sysmap.file=loc;
	    }

	    result = open_file(&params.sysmap, &file);
	    if (result != FILE_ERR_OK)
		prom_printf("\nSystem.map file not found.\n");
	    else {
		sysmap_base = prom_claim(loadinfo.base+loadinfo.memsize, 0x100000, 0);
		if (sysmap_base == (void *)-1) {
		    prom_printf("claim failed for sysmap memory\n");
		    sysmap_base = 0;
		} else {
	    	    sysmap_size = file.fs->read(&file, 0xfffff, sysmap_base);
	    	    if (sysmap_size == 0)
	    	        sysmap_base = 0;
	    	    else
	    	    	((char *)sysmap_base)[sysmap_size++] = 0;
	    	}
	    	file.fs->close(&file);
	    }
	    if (sysmap_base) {
	    	prom_printf("System.map loaded at 0x%08lx, size: %d Kbytes\n",
	    		sysmap_base, sysmap_size >> 10);
	    	loadinfo.memsize += _ALIGN(0x100000, 0x1000);
	    } else {
	    	prom_printf("System.map load failed !\n");
	    	prom_pause();
	    }
	}

	/* If ramdisk, load it. For now, we can't tell the size it will be
	 * so we claim an arbitrary amount of 4Mb
	 */
	if (params.rd.file) {
	    if(bootpath && !strcmp(bootpath,"\\\\") && params.rd.file[0] != '/')
	    {
		if (loc) free(loc);
		loc=(char*)malloc(strlen(params.rd.file)+3);
		if (!loc) {
		    prom_printf ("malloc error\n");
		    goto next;
		}
		strcpy(loc,bootpath);
		strcat(loc,params.rd.file);
		free(params.rd.file);
		params.rd.file=loc;
	    }
	    prom_printf("Loading ramdisk...\n");
	    result = open_file(&params.rd, &file);
	    if (result != FILE_ERR_OK)
		prom_printf("\nRamdisk image not found.\n");
	    else {
#define INITRD_CHUNKSIZE 0x400000
		initrd_base = prom_claim(loadinfo.base+loadinfo.memsize, INITRD_CHUNKSIZE, 0);
		if (initrd_base == (void *)-1) {
		    prom_printf("claim failed for initrd memory\n");
		    initrd_base = 0;
		} else {
	    	    initrd_size = file.fs->read(&file, INITRD_CHUNKSIZE, initrd_base);
	    	    if (initrd_size == 0)
	    	        initrd_base = 0;
                    initrd_read = initrd_size;
		    initrd_more = initrd_base;
                    while (initrd_read == INITRD_CHUNKSIZE ) { /* need to read more? */
		      initrd_want = (void *)((unsigned long)initrd_more+INITRD_CHUNKSIZE);
		      initrd_more = prom_claim(initrd_want, INITRD_CHUNKSIZE, 0);
		      if (initrd_more != initrd_want) {
			prom_printf("claim failed for initrd memory at %x rc=%x\n",initrd_want,initrd_more);
			break;
		      }
	    	    initrd_read = file.fs->read(&file, INITRD_CHUNKSIZE, initrd_more);
#if DEBUG
		    prom_printf("  block at %x rc=%x\n",initrd_more,initrd_read);
#endif
		    initrd_size += initrd_read;
		    }
	    	}
	    	file.fs->close(&file);
	    }
	    if (initrd_base)
	    	prom_printf("ramdisk loaded at 0x%08lx, size: %d Kbytes\n",
	    		initrd_base, initrd_size >> 10);
	    else {
	    	prom_printf("ramdisk load failed !\n");
	    	prom_pause();
	    }
	}

#if DEBUG
	prom_printf("setting kernel args to: %s\n", params.args);
#endif	
	prom_setargs(params.args);
#if DEBUG
	prom_printf("flushing icache...");
#endif	
	flush_icache_range ((long)loadinfo.base, (long)loadinfo.base+loadinfo.memsize);
#if DEBUG
	prom_printf(" done\n");
#endif	

	/* 
	 * Fill mew boot infos
	 *
	 * The birec is low on memory, probably inside the malloc pool, so
	 * we don't write it earlier. At this point, we should not use anything
	 * coming from the malloc pool
	 */
	birec = (struct bi_record *)_ALIGN(loadinfo.filesize+(1<<20)-1,(1<<20));

	/* We make sure it's mapped. We map only 64k for now, it's plenty enough
	 * we don't claim since this precise memory range may already be claimed
	 * by the malloc pool
	 */
	prom_map (birec, birec, 0x10000);
#if DEBUG
	prom_printf("birec at 0x%08lx\n", birec);
	{
	    int i = prom_getms();
	    while((prom_getms() - i) < 2000)
		;
	}
#endif	

	birec->tag = BI_FIRST;
	birec->size = sizeof(struct bi_record);
	birec = (struct bi_record *)((unsigned long)birec + birec->size);
	
	birec->tag = BI_BOOTLOADER_ID;
	sprintf( (char *)birec->data, "yaboot");
	birec->size = sizeof(struct bi_record) + strlen("yaboot") + 1;
	birec = (struct bi_record *)((unsigned long)birec + birec->size);
	
	birec->tag = BI_MACHTYPE;
	birec->data[0] = _machine;
	birec->size = sizeof(struct bi_record) + sizeof(unsigned long);
	birec = (struct bi_record *)((unsigned long)birec + birec->size);

	if (sysmap_base) {
		birec->tag = BI_SYSMAP;
		birec->data[0] = (unsigned long)sysmap_base;
		birec->data[1] = sysmap_size;
		birec->size = sizeof(struct bi_record) + sizeof(unsigned long)*2;
		birec = (struct bi_record *)((unsigned long)birec + birec->size);
	}
	birec->tag = BI_LAST;
	birec->size = sizeof(struct bi_record);
	birec = (struct bi_record *)((unsigned long)birec + birec->size);

	/* compute the kernel's entry point. */
	kernel_entry = loadinfo.base + loadinfo.entry - loadinfo.load_loc;

#if DEBUG
	prom_printf("Kernel entry point = 0x%08lx\n", kernel_entry);
        prom_printf("kernel: arg1 = 0x%08lx,\n"
                    "        arg2 = 0x%08lx,\n"
                    "        prom = 0x%08lx,\n"
                    "        arg4 = 0x%08lx,\n"
                    "        arg5 = 0x%08lx\n\n",
                initrd_base + loadinfo.load_loc, initrd_size, prom, 0, 0);

#endif	

#if DEBUG
	prom_printf("entering kernel...\n");
#endif
        /* call the kernel with our stack. */
        kernel_entry(initrd_base + loadinfo.load_loc, initrd_size, prom, 0, 0);
        continue;
next:
        if( file.fs != NULL )
            file.fs->close(&file);    
    }
}

static int
load_elf32(struct boot_file_t *file, loadinfo_t *loadinfo)
{
    int			i;
    Elf32_Ehdr		*e = &(loadinfo->elf.elf32hdr);
    Elf32_Phdr		*p, *ph;
    int			size = sizeof(Elf32_Ehdr) - sizeof(Elf_Ident);
    unsigned long	addr, loadaddr;

    /* Read the rest of the Elf header... */
    if ((*(file->fs->read))(file, size, &e->e_version) < size) {
	prom_printf("\nCan't read Elf32 image header\n");
	return 0;
    }

#if DEBUG
    prom_printf("Elf32 header:\n");
    prom_printf(" e.e_type      = %d\n", (int)e->e_type);
    prom_printf(" e.e_machine   = %d\n", (int)e->e_machine);
    prom_printf(" e.e_version   = %d\n", (int)e->e_version);
    prom_printf(" e.e_entry     = 0x%08x\n", (int)e->e_entry);
    prom_printf(" e.e_phoff     = 0x%08x\n", (int)e->e_phoff);
    prom_printf(" e.e_shoff     = 0x%08x\n", (int)e->e_shoff);
    prom_printf(" e.e_flags     = %d\n", (int)e->e_flags);
    prom_printf(" e.e_ehsize    = 0x%08x\n", (int)e->e_ehsize);
    prom_printf(" e.e_phentsize = 0x%08x\n", (int)e->e_phentsize);
    prom_printf(" e.e_phnum     = %d\n", (int)e->e_phnum);
#endif	    

    loadinfo->entry = e->e_entry;

    if (e->e_phnum > MAX_HEADERS) {
	prom_printf ("can only load kernels with one program header\n");
	return 0;
    }

    ph = (Elf32_Phdr *)malloc(sizeof(Elf32_Phdr) * e->e_phnum);
    if (!ph) {
	prom_printf ("malloc error\n");
	return 0;
    }

    /* Now, we read the section header */
    if ((*(file->fs->seek))(file, e->e_phoff) != FILE_ERR_OK) {
	prom_printf ("seek error\n");
	return 0;
    }
    if ((*(file->fs->read))(file, sizeof(Elf32_Phdr) * e->e_phnum, ph) !=
	    sizeof(Elf32_Phdr) * e->e_phnum) {
	prom_printf ("read error\n");
	return 0;
    }

    /* Scan through the program header
     * HACK:  We must return the _memory size of the kernel image, not the
     *        file size (because we have to leave room before other boot
     *	  infos. This code works as a side effect of the fact that
     *	  we have one section and vaddr == p_paddr
     */
    loadinfo->memsize = loadinfo->filesize = loadinfo->offset = 0;
    p = ph;
    for (i = 0; i < e->e_phnum; ++i, ++p) {
	if (p->p_type != PT_LOAD || p->p_offset == 0)
	    continue;
	if (loadinfo->memsize == 0) {
	    loadinfo->offset = p->p_offset;
	    loadinfo->memsize = p->p_memsz;
	    loadinfo->filesize = p->p_filesz;
	    loadinfo->load_loc = p->p_vaddr;
	} else {
	    loadinfo->memsize = p->p_offset + p->p_memsz - loadinfo->offset; /* XXX Bogus */
	    loadinfo->filesize = p->p_offset + p->p_filesz - loadinfo->offset;
	}
    }

    if (loadinfo->memsize == 0) {
	prom_printf("Can't find a loadable segment !\n");
	return 0;
    }

    /* leave some room (1Mb) for boot infos */
    loadinfo->memsize = _ALIGN(loadinfo->memsize,(1<<20)) + 0x100000;
    /* Claim OF memory */
#if DEBUG
    prom_printf("Before prom_claim, mem_sz: 0x%08lx\n", loadinfo->memsize);
#endif    

    /* On some systems, loadaddr may already be claimed, so try some
     * other nearby addresses before giving up.
     */
    loadaddr = (e->e_entry == KERNEL_LINK_ADDR_PPC32) ? KERNELADDR : e->e_entry;
    for(addr=loadaddr; addr <= loadaddr * 8 ;addr+=0x100000) {
	loadinfo->base = prom_claim((void *)addr, loadinfo->memsize, 0);
	if (loadinfo->base != (void *)-1) break;
    }
    if (loadinfo->base == (void *)-1) {
	prom_printf("claim error, can't allocate kernel memory\n");
	return 0;
    }	

#if DEBUG
    prom_printf("After ELF parsing, load base: 0x%08lx, mem_sz: 0x%08lx\n",
    	    loadinfo->base, loadinfo->memsize);
    prom_printf("    wanted load base: 0x%08lx, mem_sz: 0x%08lx\n",
    	    loadaddr, loadinfo->memsize);
#endif    

    /* Load the program segments... */
    p = ph;
    for (i = 0; i < e->e_phnum; ++i, ++p) {
	    unsigned long offset;
	    if (p->p_type != PT_LOAD || p->p_offset == 0)
		    continue;

	    /* Now, we skip to the image itself */
	    if ((*(file->fs->seek))(file, p->p_offset) != FILE_ERR_OK) {
		    prom_printf ("seek error\n");
		    prom_release(loadinfo->base, loadinfo->memsize);
		    return 0;
	    }
	    offset = p->p_vaddr - loadinfo->load_loc;
#ifdef CONFIG_SPLASH_SCREEN
	    if (fxReadImage(file, p->p_filesz, loadinfo->base+offset) != p->p_filesz) {
#else /* CONFIG_SPLASH_SCREEN */
	    if ((*(file->fs->read))(file, p->p_filesz, loadinfo->base+offset) != p->p_filesz) {
#endif /* CONFIG_SPLASH_SCREEN */
		    prom_printf ("read failed\n");
		    prom_release(loadinfo->base, loadinfo->memsize);
		    return 0;
	    }
    }

#if 0	/* to make editor happy */
    }
#endif	
    (*(file->fs->close))(file);

    free(ph);
    
    /* Return success at loading the Elf32 kernel */
    return 1;
}

static int
load_elf64(struct boot_file_t *file, loadinfo_t *loadinfo)
{
    int			i;
    Elf64_Ehdr		*e = &(loadinfo->elf.elf64hdr);
    Elf64_Phdr		*p, *ph;
    int			size = sizeof(Elf64_Ehdr) - sizeof(Elf_Ident);
    unsigned long	addr, loadaddr;

    /* Read the rest of the Elf header... */
    if ((*(file->fs->read))(file, size, &e->e_version) < size) {
	prom_printf("\nCan't read Elf64 image header\n");
	return 0;
    }

#if DEBUG
    prom_printf("Elf64 header:\n");
    prom_printf(" e.e_type      = %d\n", (int)e->e_type);
    prom_printf(" e.e_machine   = %d\n", (int)e->e_machine);
    prom_printf(" e.e_version   = %d\n", (int)e->e_version);
    prom_printf(" e.e_entry     = 0x%016lx\n", (long)e->e_entry);
    prom_printf(" e.e_phoff     = 0x%016lx\n", (long)e->e_phoff);
    prom_printf(" e.e_shoff     = 0x%016lx\n", (long)e->e_shoff);
    prom_printf(" e.e_flags     = %d\n", (int)e->e_flags);
    prom_printf(" e.e_ehsize    = 0x%08x\n", (int)e->e_ehsize);
    prom_printf(" e.e_phentsize = 0x%08x\n", (int)e->e_phentsize);
    prom_printf(" e.e_phnum     = %d\n", (int)e->e_phnum);
#endif	    

    loadinfo->entry = e->e_entry;

    if (e->e_phnum > MAX_HEADERS) {
	prom_printf ("can only load kernels with one program header\n");
	return 0;
    }

    ph = (Elf64_Phdr *)malloc(sizeof(Elf64_Phdr) * e->e_phnum);
    if (!ph) {
	prom_printf ("malloc error\n");
	return 0;
    }

    /* Now, we read the section header */
    if ((*(file->fs->seek))(file, e->e_phoff) != FILE_ERR_OK) {
	prom_printf ("seek error\n");
	return 0;
    }
    if ((*(file->fs->read))(file, sizeof(Elf64_Phdr) * e->e_phnum, ph) !=
	    sizeof(Elf64_Phdr) * e->e_phnum) {
	prom_printf ("read error\n");
	return 0;
    }

    /* Scan through the program header
     * HACK:  We must return the _memory size of the kernel image, not the
     *        file size (because we have to leave room before other boot
     *	  infos. This code works as a side effect of the fact that
     *	  we have one section and vaddr == p_paddr
     */
    loadinfo->memsize = loadinfo->filesize = loadinfo->offset = 0;
    p = ph;
    for (i = 0; i < e->e_phnum; ++i, ++p) {
	if (p->p_type != PT_LOAD || p->p_offset == 0)
	    continue;
	if (loadinfo->memsize == 0) {
	    loadinfo->offset = p->p_offset;
	    loadinfo->memsize = p->p_memsz;
	    loadinfo->filesize = p->p_filesz;
	    loadinfo->load_loc = p->p_vaddr;
	} else {
	    loadinfo->memsize = p->p_offset + p->p_memsz - loadinfo->offset; /* XXX Bogus */
	    loadinfo->filesize = p->p_offset + p->p_filesz - loadinfo->offset;
	}
    }

    if (loadinfo->memsize == 0) {
	prom_printf("Can't find a loadable segment !\n");
	return 0;
    }

    /* leave some room (1Mb) for boot infos */
    loadinfo->memsize = _ALIGN(loadinfo->memsize,(1<<20)) + 0x100000;
    /* Claim OF memory */
#if DEBUG
    prom_printf("Before prom_claim, mem_sz: 0x%08lx\n", loadinfo->memsize);
#endif    

    /* On some systems, loadaddr may already be claimed, so try some
     * other nearby addresses before giving up.
     */
    loadaddr = (e->e_entry == KERNEL_LINK_ADDR_PPC64) ? KERNELADDR : e->e_entry;
    for(addr=loadaddr; addr <= loadaddr * 8 ;addr+=0x100000) {
	loadinfo->base = prom_claim((void *)addr, loadinfo->memsize, 0);
	if (loadinfo->base != (void *)-1) break;
    }
    if (loadinfo->base == (void *)-1) {
	prom_printf("claim error, can't allocate kernel memory\n");
	return 0;
    }	

#if DEBUG
    prom_printf("After ELF parsing, load base: 0x%08lx, mem_sz: 0x%08lx\n",
    	    loadinfo->base, loadinfo->memsize);
    prom_printf("    wanted load base: 0x%08lx, mem_sz: 0x%08lx\n",
    	    loadaddr, loadinfo->memsize);
#endif    

    /* Load the program segments... */
    p = ph;
    for (i = 0; i < e->e_phnum; ++i, ++p) {
	    unsigned long offset;
	    if (p->p_type != PT_LOAD || p->p_offset == 0)
		    continue;

	    /* Now, we skip to the image itself */
	    if ((*(file->fs->seek))(file, p->p_offset) != FILE_ERR_OK) {
		    prom_printf ("seek error\n");
		    prom_release(loadinfo->base, loadinfo->memsize);
		    return 0;
	    }
	    offset = p->p_vaddr - loadinfo->load_loc;
#ifdef CONFIG_SPLASH_SCREEN
	    if (fxReadImage(file, p->p_filesz, loadinfo->base+offset) != p->p_filesz) {
#else /* CONFIG_SPLASH_SCREEN */
	    if ((*(file->fs->read))(file, p->p_filesz, loadinfo->base+offset) != p->p_filesz) {
#endif /* CONFIG_SPLASH_SCREEN */
		    prom_printf ("read failed\n");
		    prom_release(loadinfo->base, loadinfo->memsize);
		    return 0;
	    }
    }

#if 0	/* to make editor happy */
    }
#endif	
    (*(file->fs->close))(file);

    free(ph);
    
    /* Return success at loading the Elf64 kernel */
    return 1;
}

static int
is_elf32(loadinfo_t *loadinfo)
{
	Elf32_Ehdr *e = &(loadinfo->elf.elf32hdr);

	return (e->e_ident[EI_MAG0]  == ELFMAG0	    &&
		e->e_ident[EI_MAG1]  == ELFMAG1	    &&
		e->e_ident[EI_MAG2]  == ELFMAG2	    &&
		e->e_ident[EI_MAG3]  == ELFMAG3	    &&
		e->e_ident[EI_CLASS] == ELFCLASS32  &&
		e->e_ident[EI_DATA]  == ELFDATA2MSB &&
		e->e_type            == ET_EXEC	    &&
		e->e_machine         == EM_PPC);
}

static int
is_elf64(loadinfo_t *loadinfo)
{
	Elf64_Ehdr *e = &(loadinfo->elf.elf64hdr);

	return (e->e_ident[EI_MAG0]  == ELFMAG0	    &&
		e->e_ident[EI_MAG1]  == ELFMAG1	    &&
		e->e_ident[EI_MAG2]  == ELFMAG2	    &&
		e->e_ident[EI_MAG3]  == ELFMAG3	    &&
		e->e_ident[EI_CLASS] == ELFCLASS64  &&
		e->e_ident[EI_DATA]  == ELFDATA2MSB &&
		e->e_type            == ET_EXEC	    &&
		e->e_machine         == EM_PPC64);
}

static void
setup_display(void)
{
#ifdef CONFIG_SET_COLORMAP
	static unsigned char default_colors[] = {
		0x00, 0x00, 0x00,
		0x00, 0x00, 0xaa,
		0x00, 0xaa, 0x00,
		0x00, 0xaa, 0xaa,
		0xaa, 0x00, 0x00,
		0xaa, 0x00, 0xaa,
		0xaa, 0x55, 0x00,
		0xaa, 0xaa, 0xaa,
		0x55, 0x55, 0x55,
		0x55, 0x55, 0xff,
		0x55, 0xff, 0x55,
		0x55, 0xff, 0xff,
		0xff, 0x55, 0x55,
		0xff, 0x55, 0xff,
		0xff, 0xff, 0x55,
		0xff, 0xff, 0xff
	};
	int i, result;
	prom_handle scrn = PROM_INVALID_HANDLE;

	/* Try Apple's mac-boot screen ihandle */
  	result = (int)call_prom_return("interpret", 1, 2,
  		"\" _screen-ihandle\" $find if execute else 0 then", &scrn);
#if DEBUG
  	prom_printf("trying to get screen ihandle, result: 0x%x, scrn: 0x%x\n", result, scrn);
#endif  	
  	if (scrn == 0 || scrn == PROM_INVALID_HANDLE) {
  		char type[32];
  		/* Hrm... check to see if stdout is a display */
  		scrn = call_prom ("instance-to-package", 1, 1, prom_stdout);
#if DEBUG
	  	prom_printf("instance-to-package of stdout is: 0x%x\n", scrn);
#endif  	
  		if (prom_getprop(scrn, "device_type", type, 32) > 0 && !strncmp(type, "display", 7)) {
#if DEBUG
		  	prom_printf("got it ! stdout is a screen\n");
#endif  	
  			scrn = prom_stdout;
  		} else {
	  		/* Else, we try to open the package */
			scrn = (prom_handle)call_prom( "open", 1, 1, "screen" );
#if DEBUG
		  	prom_printf("Open screen result: 0x%x\n", scrn);
#endif  	
		}
  	}
  	
	if (scrn == PROM_INVALID_HANDLE) {
		prom_printf("no screen device found !/n");
		return;
	}
	for(i=0;i<16;i++) {
		prom_set_color(scrn, i, default_colors[i*3],
			default_colors[i*3+1], default_colors[i*3+2]);
	}
	prom_printf("\x1b[1;37m\x1b[2;40m");	
#ifdef COLOR_TEST
	for (i=0;i<16; i++) {
		prom_printf("\x1b[%d;%dm\x1b[1;47m%s \x1b[2;40m %s\n",
			ansi_color_table[i].index,
			ansi_color_table[i].value,
			ansi_color_table[i].name,
			ansi_color_table[i].name);
		prom_printf("\x1b[%d;%dm\x1b[1;37m%s \x1b[2;30m %s\n",
			ansi_color_table[i].index,
			ansi_color_table[i].value+10,
			ansi_color_table[i].name,
			ansi_color_table[i].name);
	}
	prom_printf("\x1b[1;37m\x1b[2;40m");	
#endif
#if !DEBUG
	prom_printf("\xc");
#endif	
#endif /* CONFIG_SET_COLORMAP */
}

int
yaboot_main(void)
{
	if (_machine == _MACH_Pmac)
		setup_display();
	
	prom_get_chosen("bootpath", bootdevice, sizeof(bootdevice));
#if DEBUG
	prom_printf("/chosen/bootpath = %s\n", bootdevice);
#endif	
	if (bootdevice[0] == 0)
		prom_get_options("boot-device", bootdevice, sizeof(bootdevice));
	if (bootdevice[0] == 0) {
	    prom_printf("Couldn't determine boot device\n");
	    return -1;
    	}
    	parse_device_path(bootdevice, &bootpath, &bootpartition);
#if DEBUG
	prom_printf("after parse_device_path: device:%s, path: %s, partition: %d\n",
		bootdevice, bootpath ? bootpath : NULL, bootpartition);
#endif	
  	if (bootpath) {
		if (!strncmp(bootpath, "\\\\", 2))
			bootpath[2] = 0;
		else {
			char *p, *last;
			p = last = bootpath;
			while(*p) {
			    if (*p == '\\')
			    	last = p;
			    p++;
			}
			if (p)
				*(last) = 0;
			else
				bootpath = NULL;
			if (bootpath && strlen(bootpath))
				strcat(bootpath, "\\");
		}
	}
#if DEBUG
	prom_printf("after path fixup: device:%s, path: %s, partition: %d\n",
		bootdevice, bootpath ? bootpath : NULL, bootpartition);
#endif	
	useconf = load_config_file(bootdevice, bootpath, bootpartition);

	prom_printf("Welcome to yaboot version " VERSION "\n");
	
	yaboot_text_ui();
	
	prom_printf("Bye.\n");
	return 0;
}
