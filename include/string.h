#ifndef _LINUX_STRING_H_
#define _LINUX_STRING_H_

#include "types.h"
#include "stddef.h"

extern char * ___strtok;
extern char * strcpy(char *,const char *);
extern char * strncpy(char *,const char *, size_t);
extern char * strcat(char *, const char *);
extern char * strncat(char *, const char *, size_t);
extern char * strchr(const char *,int);
extern char * strrchr(const char *,int);
extern char * strpbrk(const char *,const char *);
extern char * strtok(char *,const char *);
extern char * strstr(const char *,const char *);
extern size_t strlen(const char *);
extern size_t strspn(const char *,const char *);
extern int strcmp(const char *,const char *);
extern int strncmp(const char *,const char *,size_t);
extern int strnicmp(const char *, const char *, size_t);
extern void *strdup(char *str);

extern void * memset(void *,int,size_t);
extern void * memcpy(void *,const void *,size_t);
extern void * memmove(void *,const void *,size_t);
extern void * memscan(void *,int,size_t);
extern int memcmp(const void *,const void *,size_t);

static inline size_t strnlen(const char *s,size_t max)
{
	size_t sz = strlen(s);
	return sz > max ? max : sz;
}
#endif /* _LINUX_STRING_H_ */
