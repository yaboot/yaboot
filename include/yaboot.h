#ifndef __YABOOT_H__
#define __YABOOT_H__

#include "file.h"

struct boot_param_t {
	struct boot_fspec_t	kernel;
	struct boot_fspec_t	rd;
	struct boot_fspec_t	sysmap;

	char*	args;
};

extern int useconf;
extern char bootdevice[];
extern char *bootpath;
extern int bootpartition;

#endif
