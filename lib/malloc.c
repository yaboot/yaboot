/*  malloc.c - Dumb memory allocation routines
 *  
 *  Copyright (C) 1997 Paul Mackerras
 *                1996 Maurizio Plaza
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

#include "types.h"
#include "stddef.h"

/* Imported functions */
extern void prom_printf (char *fmt, ...);

static char *malloc_ptr = 0;
static char *malloc_top = 0;
static char *last_alloc = 0;

void malloc_init(void *bottom, unsigned long size)
{
	malloc_ptr = bottom;
	malloc_top = bottom + size;
}

void malloc_dispose(void)
{
	malloc_ptr = 0;
	last_alloc = 0;
}

void *malloc (unsigned int size)
{
    char *caddr;

    if (!malloc_ptr)
    	return NULL;
    if ((malloc_ptr + size + sizeof(int)) > malloc_top) {
	prom_printf("malloc failed\n");
    	return NULL;
    }
    *(int *)malloc_ptr = size;
    caddr = malloc_ptr + sizeof(int);
    malloc_ptr += size + sizeof(int);
    last_alloc = caddr;
    malloc_ptr = (char *) ((((unsigned int) malloc_ptr) + 3) & (~3));
    return caddr;
}

void *realloc(void *ptr, unsigned int size)
{
    char *caddr, *oaddr = ptr;

    if (!malloc_ptr)
    	return NULL;
    if (oaddr == last_alloc) {
	if (oaddr + size > malloc_top) {
		prom_printf("realloc failed\n");
		return NULL;
	}
	*(int *)(oaddr - sizeof(int)) = size;
	malloc_ptr = oaddr + size;
	return oaddr;
    }
    caddr = malloc(size);
    if (caddr != 0 && oaddr != 0)
	memcpy(caddr, oaddr, *(int *)(oaddr - sizeof(int)));
    return caddr;
}

void free (void *m)
{
    if (!malloc_ptr)
    	return;
    if (m == last_alloc)
	malloc_ptr = (char *) last_alloc - sizeof(int);
}

void mark (void **ptr)
{
    if (!malloc_ptr)
    	return;
    *ptr = (void *) malloc_ptr;
}

void release (void *ptr)
{
    if (!malloc_ptr)
    	return;
    malloc_ptr = (char *) ptr;
}

char *strdup(char const *str)
{
    char *p = malloc(strlen(str) + 1);
    if (p)
	 strcpy(p, str);
    return p;
}
