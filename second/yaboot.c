/*
 *  Yaboot - secondary boot loader for Linux on PowerPC.
 *
 *  Copyright (C) 2001, 2002 Ethan Benson
 *
 *  Copyright (C) 1999, 2000, 2001 Benjamin Herrenschmidt
 *
 *  IBM CHRP support
 *
 *  Copyright (C) 2001 Peter Bergner
 *
 *  portions based on poof
 *
 *  Copyright (C) 1999 Marius Vollmer
 *
 *  portions based on quik
 *
 *  Copyright (C) 1996 Paul Mackerras.
 *
 *  Because this program is derived from the corresponding file in the
 *  silo-0.64 distribution, it is also
 *
 *  Copyright (C) 1996 Pete A. Zaitcev
 *                1996 Maurizio Plaza
 *                1996 David S. Miller
 *                1996 Miguel de Icaza
 *                1996 Jakub Jelinek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "stdarg.h"
#include "string.h"
#include "ctype.h"
#include "stdlib.h"
#include "prom.h"
#include "file.h"
#include "errors.h"
#include "cfg.h"
#include "cmdline.h"
#include "yaboot.h"
#include "linux/elf.h"
#include "bootinfo.h"
#include "debug.h"

#define CONFIG_FILE_NAME	"yaboot.conf"
#define CONFIG_FILE_MAX		0x8000		/* 32k */

#define MESSAGE_FILE_MAX	2048

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
char bootdevice[BOOTDEVSZ];
char bootoncelabel[1024];
char bootargs[1024];
char bootlastlabel[BOOTLASTSZ] = {0};
char fw_nbr_reboots[FW_NBR_REBOOTSZ] = {0};
long  fw_reboot_cnt = 0;
char *password = NULL;
struct boot_fspec_t boot;
int _machine = _MACH_Pmac;
int flat_vmlinux;

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

static int pause_after;
static char *pause_message = "Type go<return> to continue.\n";
static char given_bootargs[1024];
static int given_bootargs_by_user = 0;

extern unsigned char linux_logo_red[];
extern unsigned char linux_logo_green[];
extern unsigned char linux_logo_blue[];

#define DEFAULT_TIMEOUT		-1

int
yaboot_start (unsigned long r3, unsigned long r4, unsigned long r5)
{
     int result;
     void* malloc_base = NULL;
     prom_handle root;

     /* Initialize OF interface */
     prom_init ((prom_entry) r5);

     prom_print_available();

     /* Allocate some memory for malloc'ator */
     malloc_base = prom_claim_chunk_top(MALLOCSIZE, 0);
     if (malloc_base == (void *)-1) {
	  prom_printf("Can't claim malloc buffer of %d bytes\n",
		      MALLOCSIZE);
	  return -1;
     }
     malloc_init(malloc_base, MALLOCSIZE);
     DEBUG_F("Malloc buffer allocated at %p (%d bytes)\n",
	     malloc_base, MALLOCSIZE);

     /* A few useless DEBUG_F's */
     DEBUG_F("reloc_offset :  %ld         (should be 0)\n", reloc_offset());
     DEBUG_F("linked at    :  0x%08x\n", TEXTADDR);

     /* ask the OF info if we're a chrp or pmac */
     /* we need to set _machine before calling finish_device_tree */
     root = prom_finddevice("/");
     if (root != 0) {
	  static char model[256];
	  if (prom_getprop(root, "CODEGEN,vendor", model, 256) > 0 &&
	      !strncmp("bplan", model, 5))
	       _machine = _MACH_bplan;
	  else if (prom_getprop(root, "device_type", model, 256 ) > 0 &&
	      !strncmp("chrp", model, 4))
	       _machine = _MACH_chrp;
	  else if (prom_getprop(root, "compatible", model, 256 ) > 0 &&
		   strstr(model, "ibm,powernv"))
	       _machine = _MACH_chrp;
	  else {
	       if (prom_getprop(root, "model", model, 256 ) > 0 &&
		   !strncmp(model, "IBM", 3))
		    _machine = _MACH_chrp;
	  }
     }

     DEBUG_F("Running on _machine = %d\n", _machine);
     DEBUG_SLEEP;

     /* Call out main */
     result = yaboot_main();

     /* Get rid of malloc pool */
     malloc_dispose();
     prom_release(malloc_base, MALLOCSIZE);
     DEBUG_F("Malloc buffer released. Exiting with code %d\n",
	     result);

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


void print_message_file(char *filename)
{
     char *msg = NULL;
     char *p, *endp;
     char *defdev = boot.dev;
     int defpart = boot.part;
     char msgpath[1024];
     int opened = 0;
     int result = 0;
     int n;
     struct boot_file_t file;
     struct boot_fspec_t msgfile;

     defdev = cfg_get_strg(0, "device");
     if (!defdev)
	  defdev = boot.dev;
     p = cfg_get_strg(0, "partition");
	  if (p) {
	       n = simple_strtol(p, &endp, 10);
	       if (endp != p && *endp == 0)
		    defpart = n;
	  }

     strncpy(msgpath, filename, sizeof(msgpath));
     msgfile = boot; /* Copy all the original paramters */
     if (!parse_device_path(msgpath, defdev, defpart, "/etc/yaboot.msg", &msgfile)) {
	  prom_printf("%s: Unable to parse\n", msgpath);
	  goto done;
     }

     result = open_file(&msgfile, &file);
     if (result != FILE_ERR_OK) {
	  prom_printf("%s:%d,", msgfile.dev, msgfile.part);
	  prom_perror(result, msgfile.file);
	  goto done;
     } else
	  opened = 1;

     msg = malloc(MESSAGE_FILE_MAX + 1);
     if (!msg)
	  goto done;
     else
	  memset(msg, 0, MESSAGE_FILE_MAX + 1);

     if (file.fs->read(&file, MESSAGE_FILE_MAX, msg) <= 0)
	  goto done;
     else
	  prom_printf("%s", msg);

done:
     if (opened)
	  file.fs->close(&file);
     if (msg)
	  free(msg);
}

/* Currently, the config file must be at the root of the filesystem.
 * todo: recognize the full path to myself and use it to load the
 * config file. Handle the "\\" (blessed system folder)
 */
static int
load_config_file(struct boot_fspec_t *fspec)
{
     char *conf_file = NULL, *p;
     struct boot_file_t file;
     int sz, opened = 0, result = 0;

     /* Allocate a buffer for the config file */
     conf_file = malloc(CONFIG_FILE_MAX);
     if (!conf_file) {
	  prom_printf("Can't alloc config file buffer\n");
	  goto bail;
     }

     /* Open it */
     result = open_file(fspec, &file);
     if (result != FILE_ERR_OK) {
	  prom_printf("%s:%d,", fspec->dev, fspec->part);
	  prom_perror(result, fspec->file);
	  prom_printf("Can't open config file\n");
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
     if (cfg_parse(fspec->file, conf_file, sz) < 0) {
	  prom_printf ("Syntax error or read error config\n");
	  goto bail;
     }

     /* 
      * set the default cf_option to label that has the same MAC addr 
      * it only works if there is a label with the MAC addr on yaboot.conf
      */
     if (prom_get_devtype(fspec->dev) == FILE_DEVICE_NET) {
         /* change the variable bellow to get the MAC dinamicaly */
         char * macaddr = NULL;
         int default_mac = 0;

         macaddr = prom_get_mac(prom_get_netinfo());
         default_mac = cfg_set_default_by_mac(macaddr);
         if (default_mac >= 1) {
            prom_printf("Default label was changed to macaddr label.\n");
         }
     }

     DEBUG_F("Config file successfully parsed, %d bytes\n", sz);

     /* Now, we do the initialisations stored in the config file */
     p = cfg_get_strg(0, "init-code");
     if (p)
	  prom_interpret(p);

     password = cfg_get_strg(0, "password");

#ifdef CONFIG_COLOR_TEXT
     p = cfg_get_strg(0, "fgcolor");
     if (p) {
	  DEBUG_F("fgcolor=%s\n", p);
	  fgcolor = check_color_text_ui(p);
	  if (fgcolor == -1) {
	       prom_printf("Invalid fgcolor: \"%s\".\n", p);
	  }
     }
     p = cfg_get_strg(0, "bgcolor");
     if (p) {
	  DEBUG_F("bgcolor=%s\n", p);
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
#endif /* !DEBUG */
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

     p = cfg_get_strg(0, "message");
     if (p)
	  print_message_file(p);

     result = 1;

bail:

     if (opened)
	  file.fs->close(&file);

     if (conf_file)
	  free(conf_file);

     return result;
}

/*
 * Search for config file by MAC address, then by IP address.
 * Basically copying pxelinux's algorithm.
 * http://syslinux.zytor.com/pxe.php#config
 */
static int load_my_config_file(struct boot_fspec_t *orig_fspec)
{
     struct bootp_packet *packet;
     int rc = 0;
     struct boot_fspec_t fspec = *orig_fspec;
     char *cfgpath = (_machine == _MACH_chrp || _machine == _MACH_bplan) ? "/etc/" : "";
     int flen;
     int minlen;

     packet = prom_get_netinfo();
     if (!packet)
          goto out;

     /*
      * First, try to match on mac address with the hardware type
      * prepended.
      */

     /* 3 chars per byte in chaddr + 2 chars for htype + /etc/ +\0 */
     fspec.file = malloc(packet->hlen * 3 + 2 + 6);
     if (!fspec.file)
	  goto out;

     sprintf(fspec.file, "%s%02x-", cfgpath, packet->htype);
     strcat(fspec.file, prom_get_mac(packet));

     rc = load_config_file(&fspec);
     if (rc)
	  goto out;

     /*
      * Now try to match on IP.
      */
     /* no need to realloc for /etc/ + 8 chars in yiaddr + \0 */
     sprintf(fspec.file, "%s%s", cfgpath, prom_get_ip(packet));

     for (flen = strlen(fspec.file),
          minlen = strlen(cfgpath); flen > minlen; flen--) {
	  rc = load_config_file(&fspec);
	  if (rc)
	       goto out;
	  /* Chop one digit off the end, try again */
	  fspec.file[flen - 1] = '\0';
     }

 out:
     if (rc) /* modify original only on success */
	  orig_fspec->file = fspec.file;
     else
	  free(fspec.file);
     return rc;
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
     int i;

     prom_printf("\n%s", str);
     for (i = 0; i < 3; i++) {
	  prom_printf ("\nPassword: ");
	  passwdbuff[0] = 0;
	  cmdedit ((void (*)(void)) 0, 1);
	  prom_printf ("\n");
#ifdef USE_MD5_PASSWORDS
	  if (!strncmp (password, "$1$", 3)) {
	       if (!check_md5_password((unsigned char*)passwdbuff, (unsigned char*)password))
		    return;
	  }
	  else if (!strcmp (password, passwdbuff))
	       return;
#else /* !MD5 */
	  if (!strcmp (password, passwdbuff))
	       return;
#endif /* USE_MD5_PASSWORDS */
	  if (i < 2) {
	       prom_sleep(1);
	       prom_printf ("Incorrect password.  Try again.");
	  }
     }
     prom_printf(" ___________________\n< Permission denied >\n -------------------\n"
		 "        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n"
		 "                ||----w |\n                ||     ||\n");
     prom_sleep(4);
     prom_interpret("reset-all");
}

int get_params(struct boot_param_t* params)
{
     int defpart;
     char *defdevice = 0;
     char defdevice_bak[1024];
     char *p, *q, *endp;
     int c, n;
     char *imagename = 0, *label;
     int timeout = -1;
     int beg = 0, end;
     int singlekey = 0;
     int restricted = 0;
     static int first = 1;
     static char imagepath[1024];
     static char initrdpath[1024];
     static char manualinitrd[1024];
     static int definitrd = 1, hasarg = 0;

     pause_after = 0;
     memset(params, 0, sizeof(*params));
     params->args = "";
     params->kernel.part = -1;
     params->rd.part = -1;
     defpart = boot.part;

     cmdinit();

     if (first && !fw_reboot_cnt) {
	  first = 0;
	  imagename = bootargs;
	  word_split(&imagename, &params->args);
	  timeout = DEFAULT_TIMEOUT;
	  if (imagename) {
	       prom_printf("Default supplied on the command line: %s ", imagename);
	       if (params->args)
		    prom_printf("%s", params->args);
	       prom_printf("\n");
	  }
	  if (useconf && (q = cfg_get_strg(0, "timeout")) != 0 && *q != 0)
	       timeout = simple_strtol(q, NULL, 0);
     }

     /* If this is a reboot due to FW detecting CAS changes then 
      * set timeout to 1.  The last kernel booted will be booted 
      * again automatically.  It should seem seamless to the user
     */
     if (fw_reboot_cnt) 
          timeout = 1;

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
	  if (!imagename) {
	       if (bootoncelabel[0] != 0)
		    imagename = bootoncelabel;
	       else if (bootlastlabel[0] != 0) {
		    imagename = bootlastlabel;
		    word_split(&imagename, &params->args);
	       } else
		    imagename = cfg_get_default();
	  }
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

     /* initrd setup via cmd console */
     /* first, check if the user uses it with some label */
     if (!strncmp(params->args, "initrd=", 7)) {
         DEBUG_F("params->args: %s\n", params->args);
         definitrd = 0;
     }
     /* after, check if there is the 'initrd=' in the imagename string */
     if (!strncmp(imagename, "initrd=", 7) || !definitrd) {

         /* return the value of definitrd to 1 */
         if (!definitrd)
             definitrd = 1;

         /* args = "initrd=blah" */
         char *args = NULL;

         if (params->args) {
            args = params->args;
            params->args = NULL;
            hasarg = 1;
         } else
            args = imagename;

         if (strlen(args)){
             /* copy the string after the '=' to manualinitrd */
             strcpy(manualinitrd, args+7);
             definitrd = 0;
             prom_printf("New initrd file specified: %s\n", manualinitrd);
         } else {
             prom_printf("ERROR: no initrd specified!\n");
             return 0;
         }

         /* set imagename with the default values of the config file */
         if ((prom_get_devtype(boot.dev) == FILE_DEVICE_NET) && !hasarg)
             imagename = cfg_get_default();
         else
             imagename = cfg_get_default();
     }

     /* chrp gets this wrong, force it -- Cort */
     if ( useconf && (!imagename || imagename[0] == 0 ))
	  imagename = cfg_get_default();

     /* write the imagename out so it can be reused on reboot if necessary */
     strcpy(bootlastlabel, imagename);
     if (params->args && params->args[0]) {
	  strcat(bootlastlabel, " ");
	  strcat(bootlastlabel, params->args);
     }
     prom_set_options("boot-last-label", bootlastlabel,
		      strlen(bootlastlabel) + 1);

     label = 0;
     defdevice = boot.dev;

     strcpy(defdevice_bak,defdevice);

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
	       if(!defdevice) defdevice=boot.dev;
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
			 check_password ("To specify arguments for this image "
					 "you must enter the password.");
		    else if (password && !restricted)
			 check_password ("This image is restricted.");
	       }
	       params->args = make_params(label, params->args);
	  }
     }

     if (!strcmp (imagename, "help")) {
          /* FIXME: defdevice shouldn't need to be reset all over the place */
	  if(!defdevice) defdevice = boot.dev;
	  prom_printf(
	       "\nPress the tab key for a list of defined images.\n"
	       "The label marked with a \"*\" is is the default image, "
	       "press <return> to boot it.\n\n"
	       "To boot any other label simply type its name and press <return>.\n\n"
	       "To boot a kernel image which is not defined in the yaboot configuration \n"
	       "file, enter the kernel image name as [[device:][partno],]/path, where \n"
	       "\"device:\" is the OpenFirmware device path to the disk the image \n"
	       "resides on, and \"partno\" is the partition number the image resides on.\n"
	       "Note that the comma (,) is only required if you specify an OpenFirmware\n"
	       "device, if you only specify a filename you should not start it with a \",\"\n\n"
           "To boot a alternative initrd file rather than specified in the yaboot\n"
           "configuration file, use the \"initrd\" command on Yaboot's prompt: \n"
           "\"initrd=[name.img]\". This will load the \"name.img\" file after the default\n"
           "kernel image. You can, also, specify a different initrd file to any other\n"
           "label of the yaboot configuration file. Just type \"label initrd=[name.img]\"\n"
           "and the specified initrd file will be loaded.\n\n"
	       "To load an alternative config file rather than /etc/yaboot.conf, enter\n"
	       "its device, partno and path, on Open Firmware Prompt:\n"
	       "boot conf=device:partno,/path/to/configfile\n."
	       "To reload the config file or load a new one, use the \"conf\" command\n"
	       "on Yaboot's prompt:\n"
	       "conf [device=device] [partition=partno] [file=/path/to/configfile]\n\n"
	       "If you omit \"device\" and \"partno\", Yaboot will use their current\n"
	       "values. You can check them by entering \"conf\" on Yaboot's prompt.\n");

	  return 0;
     }

     if (!strcmp (imagename, "halt")) {
	  if (password)
	       check_password ("Restricted command.");
	  prom_pause();
	  return 0;
     }
     if (!strcmp (imagename, "bye")) {
	  if (password) {
	       check_password ("Restricted command.");
	       return 1;
	  }
	  return 1;
     }

     if (!strncmp (imagename, "conf", 4)) {

         // imagename = "conf file=blah dev=bleh part=blih"
         DEBUG_F("Loading user-specified config file: %s\n",imagename);
         if (password) {
             check_password ("Restricted command.");
             return 1;
         }

         // args= "file=blah dev=bleh part=blih"
         char *args = params->args;

         if (strlen(args)){

            // set a pointer to the first space in args
            char *space = strchr(args,' ');

            int loop = 3;
            while (loop > 0){
                char temp[1024] = "0";

                // copy next argument to temp
                strncpy(temp, args, space-args);

                // parse temp and set boot arguments
                if (!strncmp (temp, "file=", 5)){
                   DEBUG_F("conf file: %s\n", temp+5);
                   strcpy(boot.file, temp+5);
                } else if (!strncmp (temp, "device=", 7)){
                   DEBUG_F("conf device: %s\n", temp+7);
                   strcpy(boot.dev, temp+7);
                } else if (!strncmp (temp, "partition=", 10)){
                   DEBUG_F("conf partition: %s\n", temp+10);
                   boot.part=simple_strtol(temp+10,NULL,10);
                } else
                   space = NULL;

                // set the pointer to the next space in args;
                // set the loop control variable
                if (strlen(space)>1){
                    // Go to the next argument
                    args = space+1;

                    loop--;
                    if (strchr(args,' ') == NULL)
                        space = &args[strlen(args)];
                    else
                        space = strchr(args,' ');
                } else {
                    loop = -1;
                    space = NULL;
                }
            }

            prom_printf("Loading config file...\n");
            useconf = load_config_file(&boot);
            if (useconf > 0){
                if ((q = cfg_get_strg(0, "timeout")) != 0 && *q != 0)
                   timeout = simple_strtol(q, NULL, 0);
            } else {
               prom_printf("Restoring default values.\n");
               strcpy(boot.file,"");
               strcpy(boot.dev, defdevice_bak);
               boot.part = defpart;
            }

         } else {
             prom_printf("Current configuration:\n");
             prom_printf("device: %s\n", boot.dev);
             if (boot.part < 0)
                 prom_printf("partition: auto\n");
             else
                 prom_printf("partition: %d\n", boot.part);
             if (strlen(boot.file))
                 prom_printf("file: %s\n", boot.file);
             else
                 prom_printf("file: /etc/%s\n",CONFIG_FILE_NAME);
         }

         imagename = "\0";
         params->args = "\0";

         return 0;
     }

     if (imagename[0] == '$') {
	  /* forth command string */
	  if (password)
	       check_password ("OpenFirmware commands are restricted.");
	  prom_interpret(imagename+1);
	  return 0;
     }

     strncpy(imagepath, imagename, 1024);

     if (!label && password)
	  check_password ("To boot a custom image you must enter the password.");

     params->kernel = boot; /* Copy all the original paramters */
     if (!parse_device_path(imagepath, defdevice, defpart,
			    "/vmlinux", &params->kernel)) {
	  prom_printf("%s: Unable to parse\n", imagepath);
	  return 0;
     }
     DEBUG_F("after parse_device_path: dev=%s part=%d file=%s\n", params->kernel.dev, params->kernel.part, params->kernel.file);
     if (useconf) {
	  p = cfg_get_strg(label, "initrd");
	  if (p && *p) {

           /* check if user seted to use a initrd file from boot console */
           if (!definitrd && p != manualinitrd) {
               if (manualinitrd[0] != '/' && (prom_get_devtype(defdevice_bak) != FILE_DEVICE_NET)) {
                   strcpy(initrdpath, "/");
                   strcat(initrdpath, manualinitrd);
               } else
                   strncpy(initrdpath, manualinitrd, 1024);
           } else
               strncpy(initrdpath, p, 1024);

	       DEBUG_F("Parsing initrd path <%s>\n", initrdpath);
	       params->rd = boot; /* Copy all the original paramters */
	       if (!parse_device_path(initrdpath, defdevice, defpart,
				      "/root.bin", &params->rd)) {
		    prom_printf("%s: Unable to parse\n", imagepath);
		    return 0;
	       }
	  }
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
     struct boot_file_t	file;
     int			result;
     static struct boot_param_t	params;
     void		*initrd_base;
     unsigned long	initrd_size;
     kernel_entry_t      kernel_entry;
     char*               loc=NULL;
     loadinfo_t          loadinfo;
     void                *initrd_more,*initrd_want;
     unsigned long       initrd_read;

     loadinfo.load_loc = 0;

     for (;;) {
	  initrd_size = 0;
	  initrd_base = 0;

	  if (get_params(&params))
	       return;
	  if (!params.kernel.file)
	       continue;

	  prom_printf("Please wait, loading kernel...\n");

	  memset(&file, 0, sizeof(file));

	  if (strlen(boot.file) && !strcmp(boot.file,"\\\\") && params.kernel.file[0] != '/'
	      && params.kernel.file[0] != '\\') {
	       loc=(char*)malloc(strlen(params.kernel.file)+3);
	       if (!loc) {
		    prom_printf ("malloc error\n");
		    goto next;
	       }
	       strcpy(loc,boot.file);
	       strcat(loc,params.kernel.file);
	       free(params.kernel.file);
	       params.kernel.file=loc;
	  }
	  result = open_file(&params.kernel, &file);
	  if (result != FILE_ERR_OK) {
	       prom_printf("%s:%d,", params.kernel.dev, params.kernel.part);
	       prom_perror(result, params.kernel.file);
	       goto next;
	  }

	  /* Read the Elf e_ident, e_type and e_machine fields to
	   * determine Elf file type
	   */
	  if (file.fs->read(&file, sizeof(Elf_Ident), &loadinfo.elf) < sizeof(Elf_Ident)) {
	       prom_printf("\nCan't read Elf e_ident/e_type/e_machine info\n");
	       file.fs->close(&file);
	       memset(&file, 0, sizeof(file));
	       goto next;
	  }

	  if (is_elf32(&loadinfo)) {
	       if (!load_elf32(&file, &loadinfo)) {
		    file.fs->close(&file);
		    memset(&file, 0, sizeof(file));
		    goto next;
	       }
	       prom_printf("   Elf32 kernel loaded...\n");
	  } else if (is_elf64(&loadinfo)) {
	       if (!load_elf64(&file, &loadinfo)) {
		    file.fs->close(&file);
		    memset(&file, 0, sizeof(file));
		    goto next;
	       }
	       prom_printf("   Elf64 kernel loaded...\n");
	  } else {
	       prom_printf ("%s: Not a valid ELF image\n", params.kernel.file);
	       file.fs->close(&file);
	       memset(&file, 0, sizeof(file));
	       goto next;
	  }
	  file.fs->close(&file);
	  memset(&file, 0, sizeof(file));

	  /* If ramdisk, load it (only if booting a vmlinux).  For now, we
	   * can't tell the size it will be so we claim an arbitrary amount
	   * of 4Mb.
	   */
	  if (flat_vmlinux && params.rd.file) {
	       if(strlen(boot.file) && !strcmp(boot.file,"\\\\") && params.rd.file[0] != '/'
		  && params.kernel.file[0] != '\\')
	       {
		    if (loc) free(loc);
		    loc=(char*)malloc(strlen(params.rd.file)+3);
		    if (!loc) {
			 prom_printf ("Malloc error\n");
			 goto next;
		    }
		    strcpy(loc,boot.file);
		    strcat(loc,params.rd.file);
		    free(params.rd.file);
		    params.rd.file=loc;
	       }
	       prom_printf("Loading ramdisk...\n");
	       result = open_file(&params.rd, &file);
	       if (result != FILE_ERR_OK) {
		    prom_printf("%s:%d,", params.rd.dev, params.rd.part);
		    prom_perror(result, params.rd.file);
	       }
	       else {
#define INITRD_CHUNKSIZE 0x100000
		    unsigned int len = INITRD_CHUNKSIZE;

		    /* We add a bit to the actual size so the loop below doesn't think
		     * there is more to load.
		     */
		    if (file.fs->ino_size && file.fs->ino_size(&file) > 0)
			 len = file.fs->ino_size(&file) + 0x1000;

		    initrd_base = prom_claim_chunk(loadinfo.base+loadinfo.memsize, len, 0);
		    if (initrd_base == (void *)-1) {
			 prom_printf("Claim failed for initrd memory\n");
			 initrd_base = 0;
		    } else {
			 initrd_size = file.fs->read(&file, len, initrd_base);
			 if (initrd_size == 0)
			      initrd_base = 0;
			 initrd_read = initrd_size;
			 initrd_more = initrd_base;
			 while (initrd_read == len ) { /* need to read more? */
			      initrd_want = (void *)((unsigned long)initrd_more+len);
			      initrd_more = prom_claim(initrd_want, len, 0);
			      if (initrd_more != initrd_want) {
				   prom_printf("Claim failed for initrd memory at %p rc=%p\n",initrd_want,initrd_more);
				   prom_pause();
				   break;
			      }
			      initrd_read = file.fs->read(&file, len, initrd_more);
			      DEBUG_F("  block at %p rc=%lu\n",initrd_more,initrd_read);
			      initrd_size += initrd_read;
			 }
		    }
		    file.fs->close(&file);
		    memset(&file, 0, sizeof(file));
	       }
	       if (initrd_base)
		    prom_printf("ramdisk loaded at %p, size: %lu Kbytes\n",
				initrd_base, initrd_size >> 10);
	       else {
		    prom_printf("ramdisk load failed !\n");
		    prom_pause();
	       }
	  }

	  DEBUG_F("setting kernel args to: %s\n", params.args);
	  prom_setargs(params.args);
	  DEBUG_F("flushing icache...");
	  flush_icache_range ((long)loadinfo.base, (long)loadinfo.base+loadinfo.memsize);
	  DEBUG_F(" done\n");

          /* compute the kernel's entry point. */
	  kernel_entry = loadinfo.base + loadinfo.entry - loadinfo.load_loc;

	  DEBUG_F("Kernel entry point = %p\n", kernel_entry);
	  DEBUG_F("kernel: arg1 = %p,\n"
		  "        arg2 = 0x%08lx,\n"
		  "        prom = %p,\n"
		  "        arg4 = %d,\n"
		  "        arg5 = %d\n\n",
		  initrd_base + loadinfo.load_loc, initrd_size, prom, 0, 0);

	  DEBUG_F("Entering kernel...\n");

	  prom_print_available();

          /* call the kernel with our stack. */
	  kernel_entry(initrd_base + loadinfo.load_loc, initrd_size, prom, 0, 0);
	  continue;
     next:
	  ; /* do nothing */
     }
}

static int
load_elf32(struct boot_file_t *file, loadinfo_t *loadinfo)
{
     int			i;
     Elf32_Ehdr		*e = &(loadinfo->elf.elf32hdr);
     Elf32_Phdr		*p, *ph = NULL;
     int			size = sizeof(Elf32_Ehdr) - sizeof(Elf_Ident);
     unsigned long	loadaddr;

     /* Read the rest of the Elf header... */
     if ((*(file->fs->read))(file, size, &e->e_version) < size) {
	  prom_printf("\nCan't read Elf32 image header\n");
	  goto bail;
     }

     DEBUG_F("Elf32 header:\n");
     DEBUG_F(" e.e_type      = %d\n", (int)e->e_type);
     DEBUG_F(" e.e_machine   = %d\n", (int)e->e_machine);
     DEBUG_F(" e.e_version   = %d\n", (int)e->e_version);
     DEBUG_F(" e.e_entry     = 0x%08x\n", (int)e->e_entry);
     DEBUG_F(" e.e_phoff     = 0x%08x\n", (int)e->e_phoff);
     DEBUG_F(" e.e_shoff     = 0x%08x\n", (int)e->e_shoff);
     DEBUG_F(" e.e_flags     = %d\n", (int)e->e_flags);
     DEBUG_F(" e.e_ehsize    = 0x%08x\n", (int)e->e_ehsize);
     DEBUG_F(" e.e_phentsize = 0x%08x\n", (int)e->e_phentsize);
     DEBUG_F(" e.e_phnum     = %d\n", (int)e->e_phnum);

     loadinfo->entry = e->e_entry;

     ph = (Elf32_Phdr *)malloc(sizeof(Elf32_Phdr) * e->e_phnum);
     if (!ph) {
	  prom_printf ("Malloc error\n");
	  goto bail;
     }

     /* Now, we read the section header */
     if ((*(file->fs->seek))(file, e->e_phoff) != FILE_ERR_OK) {
	  prom_printf ("seek error\n");
	  goto bail;
     }
     if ((*(file->fs->read))(file, sizeof(Elf32_Phdr) * e->e_phnum, ph) !=
	 sizeof(Elf32_Phdr) * e->e_phnum) {
	  prom_printf ("read error\n");
	  goto bail;
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
	  goto bail;
     }

     /* leave some room (1Mb) for boot infos */
     loadinfo->memsize = _ALIGN(loadinfo->memsize,(1<<20)) + 0x100000;
     /* Claim OF memory */
     DEBUG_F("Before prom_claim, mem_sz: 0x%08lx\n", loadinfo->memsize);

     /* Determine whether we are trying to boot a vmlinux or some
      * other binary image (eg, zImage).  We load vmlinux's at
      * KERNELADDR and all other binaries at their e_entry value.
      */
     if (e->e_entry == KERNEL_LINK_ADDR_PPC32) {
          flat_vmlinux = 1;
          loadaddr = KERNELADDR;
     } else {
          flat_vmlinux = 0;
          loadaddr = loadinfo->load_loc;
     }

     loadinfo->base = prom_claim_chunk((void *)loadaddr, loadinfo->memsize, 0);
     if (loadinfo->base == (void *)-1) {
	  prom_printf("Claim error, can't allocate kernel memory\n");
	  goto bail;
     }

     DEBUG_F("After ELF parsing, load base: %p, mem_sz: 0x%08lx\n",
	     loadinfo->base, loadinfo->memsize);
     DEBUG_F("    wanted load base: 0x%08lx, mem_sz: 0x%08lx\n",
	     loadaddr, loadinfo->memsize);

     /* Load the program segments... */
     p = ph;
     for (i = 0; i < e->e_phnum; ++i, ++p) {
	  unsigned long offset;
	  if (p->p_type != PT_LOAD || p->p_offset == 0)
	       continue;

	  /* Now, we skip to the image itself */
	  if ((*(file->fs->seek))(file, p->p_offset) != FILE_ERR_OK) {
	       prom_printf ("Seek error\n");
	       prom_release(loadinfo->base, loadinfo->memsize);
	       goto bail;
	  }
	  offset = p->p_vaddr - loadinfo->load_loc;
	  if ((*(file->fs->read))(file, p->p_filesz, loadinfo->base+offset) != p->p_filesz) {
	       prom_printf ("Read failed\n");
	       prom_release(loadinfo->base, loadinfo->memsize);
	       goto bail;
	  }
     }

     free(ph);

     /* Return success at loading the Elf32 kernel */
     return 1;

bail:
     if (ph)
       free(ph);
     return 0;
}

static int
load_elf64(struct boot_file_t *file, loadinfo_t *loadinfo)
{
     int			i;
     Elf64_Ehdr		*e = &(loadinfo->elf.elf64hdr);
     Elf64_Phdr		*p, *ph = NULL;
     int			size = sizeof(Elf64_Ehdr) - sizeof(Elf_Ident);
     unsigned long	loadaddr;

     /* Read the rest of the Elf header... */
     if ((*(file->fs->read))(file, size, &e->e_version) < size) {
	  prom_printf("\nCan't read Elf64 image header\n");
	  goto bail;
     }

     DEBUG_F("Elf64 header:\n");
     DEBUG_F(" e.e_type      = %d\n", (int)e->e_type);
     DEBUG_F(" e.e_machine   = %d\n", (int)e->e_machine);
     DEBUG_F(" e.e_version   = %d\n", (int)e->e_version);
     DEBUG_F(" e.e_entry     = 0x%016lx\n", (long)e->e_entry);
     DEBUG_F(" e.e_phoff     = 0x%016lx\n", (long)e->e_phoff);
     DEBUG_F(" e.e_shoff     = 0x%016lx\n", (long)e->e_shoff);
     DEBUG_F(" e.e_flags     = %d\n", (int)e->e_flags);
     DEBUG_F(" e.e_ehsize    = 0x%08x\n", (int)e->e_ehsize);
     DEBUG_F(" e.e_phentsize = 0x%08x\n", (int)e->e_phentsize);
     DEBUG_F(" e.e_phnum     = %d\n", (int)e->e_phnum);

     loadinfo->entry = e->e_entry;

     ph = (Elf64_Phdr *)malloc(sizeof(Elf64_Phdr) * e->e_phnum);
     if (!ph) {
	  prom_printf ("Malloc error\n");
	  goto bail;
     }

     /* Now, we read the section header */
     if ((*(file->fs->seek))(file, e->e_phoff) != FILE_ERR_OK) {
	  prom_printf ("Seek error\n");
	  goto bail;
     }
     if ((*(file->fs->read))(file, sizeof(Elf64_Phdr) * e->e_phnum, ph) !=
	 sizeof(Elf64_Phdr) * e->e_phnum) {
	  prom_printf ("Read error\n");
	  goto bail;
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
	  goto bail;
     }

     loadinfo->memsize = _ALIGN(loadinfo->memsize,(1<<20));
     /* Claim OF memory */
     DEBUG_F("Before prom_claim, mem_sz: 0x%08lx\n", loadinfo->memsize);

     /* Determine whether we are trying to boot a vmlinux or some
      * other binary image (eg, zImage).  We load vmlinux's at
      * KERNELADDR and all other binaries at their e_entry value.
      */
     if (e->e_entry == KERNEL_LINK_ADDR_PPC64) {
          flat_vmlinux = 1;
          loadaddr = KERNELADDR;
     } else {
          flat_vmlinux = 0;
          loadaddr = e->e_entry;
     }

     loadinfo->base = prom_claim_chunk((void *)loadaddr, loadinfo->memsize, 0);
     if (loadinfo->base == (void *)-1) {
	  prom_printf("Claim error, can't allocate kernel memory\n");
	  goto bail;
     }

     DEBUG_F("After ELF parsing, load base: %p, mem_sz: 0x%08lx\n",
	     loadinfo->base, loadinfo->memsize);
     DEBUG_F("    wanted load base: 0x%08lx, mem_sz: 0x%08lx\n",
	     loadaddr, loadinfo->memsize);

     /* Load the program segments... */
     p = ph;
     for (i = 0; i < e->e_phnum; ++i, ++p) {
	  unsigned long offset;
	  if (p->p_type != PT_LOAD || p->p_offset == 0)
	       continue;

	  /* Now, we skip to the image itself */
	  if ((*(file->fs->seek))(file, p->p_offset) != FILE_ERR_OK) {
	       prom_printf ("Seek error\n");
	       prom_release(loadinfo->base, loadinfo->memsize);
	       goto bail;
	  }
	  offset = p->p_vaddr - loadinfo->load_loc;
	  if ((*(file->fs->read))(file, p->p_filesz, loadinfo->base+offset) != p->p_filesz) {
	       prom_printf ("Read failed\n");
	       prom_release(loadinfo->base, loadinfo->memsize);
	       goto bail;
	  }
     }

     free(ph);

     /* Return success at loading the Elf64 kernel */
     return 1;

bail:
     if (ph)
       free(ph);
     return 0;
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
	     (e->e_type == ET_EXEC || e->e_type == ET_DYN) &&
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
     int i, result __attribute__((unused));
     prom_handle scrn = PROM_INVALID_HANDLE;

     /* Try Apple's mac-boot screen ihandle */
     result = (int)call_prom_return("interpret", 1, 2,
				    "\" _screen-ihandle\" $find if execute else 0 then", &scrn);
     DEBUG_F("Trying to get screen ihandle, result: %d, scrn: %p\n", result, scrn);

     if (scrn == 0 || scrn == PROM_INVALID_HANDLE) {
	  char type[32];
	  /* Hrm... check to see if stdout is a display */
	  scrn = call_prom ("instance-to-package", 1, 1, prom_stdout);
	  DEBUG_F("instance-to-package of stdout is: %p\n", scrn);
	  if (prom_getprop(scrn, "device_type", type, 32) > 0 && !strncmp(type, "display", 7)) {
	       DEBUG_F("got it ! stdout is a screen\n");
	       scrn = prom_stdout;
	  } else {
	       /* Else, we try to open the package */
	       scrn = (prom_handle)call_prom( "open", 1, 1, "screen" );
	       DEBUG_F("Open screen result: %p\n", scrn);
	  }
     }

     if (scrn == PROM_INVALID_HANDLE) {
	  prom_printf("No screen device found !\n");
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
#endif /* COLOR_TEST */

#if !DEBUG
     prom_printf("\xc");
#endif /* !DEBUG */

#endif /* CONFIG_SET_COLORMAP */
}

int
yaboot_main(void)
{
     char *ptype;
     char *endp;
     int conf_given = 0;
     char conf_path[1024];

     if (_machine == _MACH_Pmac)
	  setup_display();

     prom_get_chosen("bootargs", bootargs, sizeof(bootargs));
     DEBUG_F("/chosen/bootargs = %s\n", bootargs);
     prom_get_chosen("bootpath", bootdevice, BOOTDEVSZ);
     DEBUG_F("/chosen/bootpath = %s\n", bootdevice);
     if (prom_get_options("ibm,client-architecture-support-reboot",fw_nbr_reboots, FW_NBR_REBOOTSZ) == -1 )
        prom_get_options("ibm,fw-nbr-reboots",fw_nbr_reboots, FW_NBR_REBOOTSZ);
     fw_reboot_cnt = simple_strtol(fw_nbr_reboots,&endp,10);
     if (fw_reboot_cnt > 0L)
          prom_get_options("boot-last-label", bootlastlabel, BOOTLASTSZ);

     /* If conf= specified on command line, it overrides
        Usage: conf=device:partition,/path/to/conffile
        Example: On Open Firmware Prompt, type
                 boot conf=/pci@8000000f8000000/pci@1/pci1014,028C@1/scsi@0/sd@1,0:3,/etc/yaboot.conf */

     if (!strncmp(bootargs, "conf=", 5)) {
        DEBUG_F("Using conf argument in Open Firmware\n");
        char *end = strchr(bootargs,' ');
        if (end)
            *end = 0;

        strcpy(bootdevice, bootargs + 5);
        conf_given = 1;
        DEBUG_F("Using conf=%s\n", bootdevice);

        /* Remove conf=xxx from bootargs */
        if (end)
            memmove(bootargs, end+1, strlen(end+1)+1);
        else
            bootargs[0] = 0;
     }
     if (bootdevice[0] == 0) {
	  prom_get_options("boot-device", bootdevice, BOOTDEVSZ);
	  DEBUG_F("boot-device = %s\n", bootdevice);
     }
     if (bootdevice[0] == 0) {
	  prom_printf("Couldn't determine boot device\n");
	  return -1;
     }

     if (bootoncelabel[0] == 0) {
	  prom_get_options("boot-once", bootoncelabel, 
			   sizeof(bootoncelabel));
     	  if (bootoncelabel[0] != 0)
		DEBUG_F("boot-once: [%s]\n", bootoncelabel);
     }
     prom_set_options("boot-once", NULL, 0);

     if (!parse_device_path(bootdevice, NULL, -1, "", &boot)) {
	  prom_printf("%s: Unable to parse\n", bootdevice);
	  return -1;
     }
     if (_machine == _MACH_bplan && !conf_given)
        boot.part++;
     DEBUG_F("After parse_device_path: dev=%s, part=%d, file=%s\n",
	     boot.dev, boot.part, boot.file);

     if (!conf_given) {
         if (_machine == _MACH_chrp || _machine == _MACH_bplan)
             boot.file = "/etc/";
         else if (strlen(boot.file)) {
             if (!strncmp(boot.file, "\\\\", 2))
                 boot.file = "\\\\";
             else {
                 char *p, *last;
                 p = last = boot.file;
                 while(*p) {
                     if (*p == '\\')
                         last = p;
                     p++;
                 }
                 if (p)
                     *(last) = 0;
                 else
                     boot.file = "";
                 if (strlen(boot.file))
                     strcat(boot.file, "\\");
             }
         }
         strcpy(conf_path, boot.file);
         strcat(conf_path, CONFIG_FILE_NAME);
         boot.file = conf_path;
         DEBUG_F("After path kludgeup: dev=%s, part=%d, file=%s\n",
            boot.dev, boot.part, boot.file);
     }

     /*
      * If we're doing a netboot, first look for one which matches our
      * MAC address.
      */
     if (prom_get_devtype(boot.dev) == FILE_DEVICE_NET) {
          prom_printf("Try to netboot\n");
	  useconf = load_my_config_file(&boot);
     }

     if (!useconf)
         useconf = load_config_file(&boot);

     prom_printf("Welcome to yaboot version " VERSION "\n");
     prom_printf("Enter \"help\" to get some basic usage information\n");

     /* I am fed up with lusers using the wrong partition type and
	mailing me *when* it breaks */

     if (_machine == _MACH_Pmac) {
	  char *entry = cfg_get_strg(0, "ptypewarning");
	  int warn = 1;
	  if (entry)
	       warn = strcmp(entry,
			     "I_know_the_partition_type_is_wrong_and_will_NOT_send_mail_when_booting_breaks");
	  if (warn) {
	       ptype = get_part_type(boot.dev, boot.part);
	       if ((ptype != NULL) && (strcmp(ptype, "Apple_Bootstrap")))
		    prom_printf("\nWARNING: Bootstrap partition type is wrong: \"%s\"\n"
				"         type should be: \"Apple_Bootstrap\"\n\n", ptype);
	       if (ptype)
		    free(ptype);
	  }
     }

     yaboot_text_ui();

     prom_printf("Bye.\n");
     return 0;
}

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
