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

#include "stdarg.h"

typedef void *prom_handle;
typedef void *ihandle;
typedef void *phandle;

#define PROM_INVALID_HANDLE	((prom_handle)-1UL)

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
void prom_vprintf (char *fmt, va_list ap) __attribute__ ((format (printf, 1, 0)));
void prom_fprintf (prom_handle dev, char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void prom_printf (char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
#else
void prom_vprintf (char *fmt, va_list ap);
void prom_fprintf (prom_handle dev, char *fmt, ...);
void prom_printf (char *fmt, ...);
#endif

void prom_perror (int error, char *filename);
void prom_readline (char *prompt, char *line, int len);
int prom_set_color(prom_handle device, int color, int r, int g, int b);

/* memory */

void *prom_claim (void *virt, unsigned int size, unsigned int align);
void prom_release(void *virt, unsigned int size);
void prom_map (void *phys, void *virt, int size);

/* packages and device nodes */

prom_handle prom_finddevice (char *name);
prom_handle prom_findpackage (char *path);
int prom_getprop (prom_handle dev, char *name, void *buf, int len);
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

extern int prom_getms(void);
extern void prom_pause(void);

extern void *call_prom (const char *service, int nargs, int nret, ...);
extern void *call_prom_return (const char *service, int nargs, int nret, ...);

#endif
