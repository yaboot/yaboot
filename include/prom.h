/*
 *  prom.h - Routines for talking to the Open Firmware PROM
 *
 *  Copyright (C) 2001 Ethan Benson
 *
 *  Copyright (C) 1999 Benjamin Herrenschmidt
 *
 *  Copyright (C) 1999 Marius Vollmer
 *
 *  Copyright (C) 1996 Paul Mackerras.
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

#ifndef PROM_H
#define PROM_H

#include "types.h"
#include "stdarg.h"

typedef void *prom_handle;
typedef void *ihandle;
typedef void *phandle;

#define PROM_INVALID_HANDLE	((prom_handle)-1UL)
#define BOOTDEVSZ               (2048) /* iscsi args can be in excess of 1040 bytes */
#define TOK_ISCSI               "iscsi"
#define TOK_IPV6                "ipv6"
#define PROM_CLAIM_MAX_ADDR	0x10000000
#define BOOTLASTSZ		1024
#define FW_NBR_REBOOTSZ		4

struct prom_args;
typedef int (*prom_entry)(struct prom_args *);

extern void prom_init (prom_entry pe);

extern prom_entry prom;

/* I/O */

extern prom_handle prom_stdin;
extern prom_handle prom_stdout;

prom_handle prom_open (char *spec);
int prom_read (prom_handle file, void *buf, int len);
int prom_write (prom_handle file, void *buf, int len);
int prom_seek (prom_handle file, int pos);
int prom_lseek (prom_handle file, unsigned long long pos);
int prom_readblocks (prom_handle file, int blockNum, int blockCount, void *buffer);
void prom_close (prom_handle file);
int prom_getblksize (prom_handle file);
int prom_loadmethod (prom_handle device, void* addr);

#define K_UP    0x141
#define K_DOWN  0x142
#define K_LEFT  0x144
#define K_RIGHT 0x143

int prom_getchar ();
void prom_putchar (char);
int prom_nbgetchar();

#ifdef __GNUC__
void prom_vprintf (const char *fmt, va_list ap) __attribute__ ((format (printf, 1, 0)));
void prom_vfprintf (prom_handle file, const char *fmt, va_list ap)  __attribute__ ((format (printf, 2, 0)));
void prom_fprintf (prom_handle dev, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void prom_printf (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
void prom_debug (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#else
void prom_vprintf (const char *fmt, va_list ap);
void prom_fprintf (prom_handle dev, const char *fmt, ...);
void prom_printf (const char *fmt, ...);
void prom_debug (const char *fmt, ...);
#endif

void prom_perror (int error, char *filename);
void prom_readline (char *prompt, char *line, int len);
int prom_set_color(prom_handle device, int color, int r, int g, int b);

/* memory */

void *prom_claim_chunk(void *virt, unsigned int size, unsigned int align);
void *prom_claim_chunk_top(unsigned int size, unsigned int align);
void *prom_claim (void *virt, unsigned int size, unsigned int align);
void prom_release(void *virt, unsigned int size);
void prom_map (void *phys, void *virt, int size);
void prom_print_available(void);

/* packages and device nodes */

prom_handle prom_finddevice (char *name);
prom_handle prom_findpackage (char *path);
int prom_getprop (prom_handle dev, char *name, void *buf, int len);
int prom_setprop (prom_handle dev, char *name, void *buf, int len);
int prom_getproplen(prom_handle, const char *);
int prom_get_devtype (char *device);

/* misc */

char *prom_getargs ();
void prom_setargs (char *args);

void prom_exit ();
void prom_abort (char *fmt, ...);
void prom_sleep (int seconds);

int prom_interpret (char *forth);

int prom_get_chosen (char *name, void *mem, int len);
int prom_get_options (char *name, void *mem, int len);
int prom_set_options (char *name, void *mem, int len);

extern int prom_getms(void);
extern void prom_pause(void);

extern void *call_prom (const char *service, int nargs, int nret, ...);
extern void *call_prom_return (const char *service, int nargs, int nret, ...);

/* Netboot stuffs */

/*
 * "bootp-response" is the property name which is specified in
 * the recommended practice doc for obp-tftp. However, pmac
 * provides a "dhcp-response" property and chrp provides a
 * "bootpreply-packet" property.  The latter appears to begin the
 * bootp packet at offset 0x2a in the property for some reason.
 */

struct bootp_property_offset {
     char *name; /* property name */
     int offset; /* offset into property where bootp packet occurs */
};

static const struct bootp_property_offset bootp_response_properties[] = {
     { .name = "bootp-response", .offset = 0 },
     { .name = "dhcp-response", .offset = 0 },
     { .name = "bootpreply-packet", .offset = 0x2a },
};

struct bootp_packet {
     __u8 op, htype, hlen, hops;
     __u32 xid;
     __u16 secs, flags;
     __u32 ciaddr, yiaddr, siaddr, giaddr;
     unsigned char chaddr[16];
     unsigned char sname[64];
     unsigned char file[128];
     unsigned char options[]; /* vendor options */
};

struct bootp_packet * prom_get_netinfo (void);
char * prom_get_mac (struct bootp_packet * packet);
char * prom_get_ip (struct bootp_packet * packet);

#endif
