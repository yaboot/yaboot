/*
 * include/stdlib.h
 *
 */
#ifndef __STDLIB_H
#define __STDLIB_H

#include "stdarg.h"

extern void malloc_init(void *bottom, unsigned long size);
extern void malloc_dispose(void);

extern void *malloc(unsigned int size);
extern void *realloc(void *ptr, unsigned int size);
extern void free (void *m);
extern void mark (void **ptr);
extern void release (void *ptr);

extern int sprintf(char * buf, const char *fmt, ...);
extern int vsprintf(char *buf, const char *fmt, va_list args);
extern long simple_strtol(const char *cp,char **endp,unsigned int base);
#define strtol(x,y,z) simple_strtol(x,y,z)

#endif
