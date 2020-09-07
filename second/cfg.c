/*
 *  cfg.c - Handling and parsing of yaboot.conf
 *
 *  Copyright (C) 1995 Werner Almesberger
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

#include "setjmp.h"
#include "stdarg.h"
#include "stdlib.h"
#include "string.h"
#include "types.h"
#include "prom.h"

/* Imported functions */
extern int strcasecmp(const char *s1, const char *s2);
extern int strncasecmp(const char *cs, const char *ct, size_t n);
extern char bootoncelabel[1024];

typedef enum {
     cft_strg, cft_flag, cft_end
} CONFIG_TYPE;

typedef struct {
     CONFIG_TYPE type;
     char *name;
     void *data;
} CONFIG;

#define MAX_TOKEN 511
#define EOF -1

CONFIG cf_options[] =
{
     {cft_strg, "device", NULL},
     {cft_strg, "partition", NULL},
     {cft_strg, "default", NULL},
     {cft_strg, "timeout", NULL},
     {cft_strg, "password", NULL},
     {cft_flag, "restricted", NULL},
     {cft_strg, "message", NULL},
     {cft_strg, "root", NULL},
     {cft_strg, "ramdisk", NULL},
     {cft_flag, "read-only", NULL},
     {cft_flag, "read-write", NULL},
     {cft_strg, "append", NULL},
     {cft_strg, "initrd", NULL},
     {cft_flag, "initrd-prompt", NULL},
     {cft_strg, "initrd-size", NULL},
     {cft_flag, "pause-after", NULL},
     {cft_strg, "pause-message", NULL},
     {cft_strg, "init-code", NULL},
     {cft_strg, "init-message", NULL},
     {cft_strg, "fgcolor", NULL},
     {cft_strg, "bgcolor", NULL},
     {cft_strg, "ptypewarning", NULL},
     {cft_end, NULL, NULL}};

CONFIG cf_image[] =
{
     {cft_strg, "image", NULL},
     {cft_strg, "label", NULL},
     {cft_strg, "alias", NULL},
     {cft_flag, "single-key", NULL},
     {cft_flag, "restricted", NULL},
     {cft_strg, "device", NULL},
     {cft_strg, "partition", NULL},
     {cft_strg, "root", NULL},
     {cft_strg, "ramdisk", NULL},
     {cft_flag, "read-only", NULL},
     {cft_flag, "read-write", NULL},
     {cft_strg, "append", NULL},
     {cft_strg, "literal", NULL},
     {cft_strg, "initrd", NULL},
     {cft_flag, "initrd-prompt", NULL},
     {cft_strg, "initrd-size", NULL},
     {cft_flag, "pause-after", NULL},
     {cft_strg, "pause-message", NULL},
     {cft_flag, "novideo", NULL},
     {cft_end, NULL, NULL}};

static char flag_set;
static char *last_token = NULL, *last_item = NULL, *last_value = NULL;
static int line_num;
static int back = 0;		/* can go back by one char */
static char *currp = NULL;
static char *endp = NULL;
static char *file_name = NULL;
static CONFIG *curr_table = cf_options;
static jmp_buf env;

static struct IMAGES {
     CONFIG table[sizeof (cf_image) / sizeof (cf_image[0])];
     int obsolete;
     struct IMAGES *next;
} *images = NULL;

void cfg_error (char *msg,...)
{
     va_list ap;

     va_start (ap, msg);
     prom_printf ("Config file error: ");
     prom_vprintf (msg, ap);
     va_end (ap);
     prom_printf (" near line %d in file %s\n", line_num, file_name);
     longjmp (env, 1);
}

void cfg_warn (char *msg,...)
{
     va_list ap;

     va_start (ap, msg);
     prom_printf ("Config file warning: ");
     prom_vprintf (msg, ap);
     va_end (ap);
     prom_printf (" near line %d in file %s\n", line_num, file_name);
}

int getc ()
{
     if (currp == endp)
	  return EOF;
     return *currp++;
}

#define next_raw next
static int next (void)
{
     int ch;

     if (!back)
	  return getc ();
     ch = back;
     back = 0;
     return ch;
}

static void again (int ch)
{
     back = ch;
}

static char *cfg_get_token (void)
{
     char buf[MAX_TOKEN + 1];
     char *here;
     int ch, escaped;

     if (last_token) {
	  here = last_token;
	  last_token = NULL;
	  return here;
     }
     while (1) {
	  while (ch = next (), ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
	       if (ch == '\n' || ch == '\r')
		    line_num++;
	  if (ch == EOF || ch == (int)NULL)
	       return NULL;
	  if (ch != '#')
	       break;
	  while (ch = next_raw (), (ch != '\n' && ch != '\r'))
	       if (ch == EOF)
		    return NULL;
	  line_num++;
     }
     if (ch == '=')
	  return strdup ("=");
     if (ch == '"') {
	  here = buf;
	  while (here - buf < MAX_TOKEN) {
	       if ((ch = next ()) == EOF)
		    cfg_error ("EOF in quoted string");
	       if (ch == '"') {
		    *here = 0;
		    return strdup (buf);
	       }
	       if (ch == '\\') {
		    ch = next ();
		    switch (ch) {
		    case '"':
		    case '\\':
			 break;
		    case '\n':
		    case '\r':
			 while ((ch = next ()), ch == ' ' || ch == '\t');
			 if (!ch)
			      continue;
			 again (ch);
			 ch = ' ';
			 break;
		    case 'n':
			 ch = '\n';
			 break;
		    default:
			 cfg_error ("Bad use of \\ in quoted string");
		    }
	       } else if ((ch == '\n') || (ch == '\r'))
		    cfg_error ("newline is not allowed in quoted strings");
	       *here++ = ch;
	  }
	  cfg_error ("Quoted string is too long");
	  return 0;		/* not reached */
     }
     here = buf;
     escaped = 0;
     while (here - buf < MAX_TOKEN) {
	  if (escaped) {
	       if (ch == EOF)
		    cfg_error ("\\ precedes EOF");
	       if (ch == '\n')
		    line_num++;
	       else
		    *here++ = ch == '\t' ? ' ' : ch;
	       escaped = 0;
	  } else {
	       if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '#' ||
		   ch == '=' || ch == EOF) {
		    again (ch);
		    *here = 0;
		    return strdup (buf);
	       }
	       if (!(escaped = (ch == '\\')))
		    *here++ = ch;
	  }
	  ch = next ();
     }
     cfg_error ("Token is too long");
     return 0;			/* not reached */
}

static void cfg_return_token (char *token)
{
     last_token = token;
}

static int cfg_next (char **item, char **value)
{
     char *this;

     if (last_item) {
	  *item = last_item;
	  *value = last_value;
	  last_item = NULL;
	  return 1;
     }
     *value = NULL;
     if (!(*item = cfg_get_token ()))
	  return 0;
     if (!strcmp (*item, "="))
	  cfg_error ("Syntax error");
     if (!(this = cfg_get_token ()))
	  return 1;
     if (strcmp (this, "=")) {
	  cfg_return_token (this);
	  return 1;
     }
     if (!(*value = cfg_get_token ()))
	  cfg_error ("Value expected at EOF");
     if (!strcmp (*value, "="))
	  cfg_error ("Syntax error after %s", *item);
     return 1;
}

static char *cfg_get_strg_i (CONFIG * table, char *item)
{
     CONFIG *walk;

     for (walk = table; walk->type != cft_end; walk++)
          if (walk->name && !strcasecmp (walk->name, item))
               return walk->data;
     return 0;
}

#if 0
// The one and only call to this procedure is commented out
// below, so we don't need this unless we decide to use it again.
static void cfg_return (char *item, char *value)
{
     last_item = item;
     last_value = value;
}
#endif

static int match_arch(const char *name)
{
	static prom_handle root;
	static char model[256], *p;

	if (!root) {
		if (!(root = prom_finddevice("/")))
			return 0;
	}

	if (!model[0]) {
		if (!prom_getprop(root, "compatible", model, sizeof(model)))
			return 0;
	}

	for (p = model; *p; p += strlen(p) + 1) {
		if (!strcasecmp(p, name))
			return 1;
	}

	return 0;
}

static void check_for_obsolete(const char *label)
{
	struct IMAGES *p;
	char *cur_label;

	/* Make sure our current entry isn't obsolete (ignored) */
	for (p = images; p; p = p->next) {
		if (curr_table == p->table && p->obsolete)
			return;
	}

	for (p = images; p; p = p->next) {
		if (curr_table == p->table)
			continue;

		cur_label = cfg_get_strg_i (p->table, "label");
		if (!cur_label)
			cur_label = cfg_get_strg_i (p->table, "image");

		if (!cur_label)
			continue;

		if (!strcasecmp(cur_label, label))
			p->obsolete = 1;
	}
}

static int cfg_set (char *item, char *value)
{
     CONFIG *walk;

     if (!strncasecmp (item, "image", 5)) {
	  struct IMAGES **p = &images;
	  int ignore = 0;

	  if (item[5] == '[' && item[strlen(item) - 1] == ']') {
		char *s, *q = item;

		/* Get rid of braces */
		item[strlen(item) - 1] = 0;
		item[5] = 0;

		for (s = item + 6; q; s = q) {
			q = strchr(s, '|');
			if (q)
				*q++ = 0;

			if (match_arch(s))
				goto cfg_set_cont;
                }
		/* This just creates an unused table. It will get ignored */
		ignore = 1;
	  } else if (item[5])
                goto cfg_set_redo;

cfg_set_cont:
	  while (*p)
	       p = &((*p)->next);
	  *p = (struct IMAGES *)malloc (sizeof (struct IMAGES));
	  if (*p == NULL) {
	       prom_printf("malloc error in cfg_set\n");
	       return -1;
	  }
	  (*p)->next = 0;
	  (*p)->obsolete = ignore;
	  curr_table = ((*p)->table);
	  memcpy (curr_table, cf_image, sizeof (cf_image));
     }

cfg_set_redo:
     for (walk = curr_table; walk->type != cft_end; walk++) {
	  if (walk->name && !strcasecmp (walk->name, item)) {
	       if (value && walk->type != cft_strg)
		    cfg_warn ("'%s' doesn't have a value", walk->name);
	       else if (!value && walk->type == cft_strg)
		    cfg_warn ("Value expected for '%s'", walk->name);
	       else {
		    if (!strcasecmp (item, "label"))
			 check_for_obsolete(value);
		    if (walk->data)
			 cfg_warn ("Duplicate entry '%s'", walk->name);
		    if (walk->type == cft_flag)
			 walk->data = &flag_set;
		    else if (walk->type == cft_strg)
			 walk->data = value;
	       }
	       break;
	  }
     }

     if (walk->type != cft_end)
	  return 1;

     //cfg_return (item, value);

     return 0;
}

static int cfg_reset ()
{
    CONFIG *walk;
#if DEBUG
    prom_printf("Resetting image table\n");
#endif
    line_num = 0;
    images = NULL;
    curr_table = NULL;
    curr_table = cf_options;
    for (walk = curr_table; walk->type != cft_end; walk++) {
#if DEBUG
        prom_printf("ItemA %s = %s\n", walk->name, (char *)walk->data);
#endif
        if (walk->data != NULL)
            walk->data = NULL;
#if DEBUG
        prom_printf("ItemB %s = %s\n\n", walk->name, (char *)walk->data);
#endif
    }

    return 0;
}

int cfg_parse (char *cfg_file, char *buff, int len)
{
     char *item, *value;

     file_name = cfg_file;
     currp = buff;
     endp = currp + len;

     cfg_reset();

     if (setjmp (env))
	  return -1;
     while (1) {
	  if (!cfg_next (&item, &value))
	       return 0;
	  if (!cfg_set (item, value)) {
#if DEBUG
	       prom_printf("Can't set item %s to value %s\n", item, value);
#endif
	  }
	  free (item);
     }
}

char *cfg_get_strg (char *image, char *item)
{
     struct IMAGES *p;
     char *label, *alias;
     char *ret;

     if (!image)
	  return cfg_get_strg_i (cf_options, item);
     for (p = images; p; p = p->next) {
	  if (p->obsolete)
		  continue;

	  label = cfg_get_strg_i (p->table, "label");
	  if (!label) {
	       label = cfg_get_strg_i (p->table, "image");
	       alias = strrchr (label, '/');
	       if (alias)
		    label = alias + 1;
	  }
	  alias = cfg_get_strg_i (p->table, "alias");
	  if (!strcmp (label, image) || (alias && !strcmp (alias, image))) {
	       ret = cfg_get_strg_i (p->table, item);
	       if (!ret)
		    ret = cfg_get_strg_i (cf_options, item);
	       return ret;
	  }
     }
     return 0;
}

int cfg_get_flag (char *image, char *item)
{
     return !!cfg_get_strg (image, item);
}

static int printl_count = 0;
static void printlabel (char *label, int defflag)
{
     int len = strlen (label);
     char a = ' ';

     if (!printl_count)
	  prom_printf ("\n");
     switch (defflag) {
	  case 1:  a='*'; break;
	  case 2:  a='&'; break;
	  default: a=' '; break;
     }
     prom_printf ("%c %s", a, label);
     while (len++ < 25)
	  prom_putchar (' ');
     printl_count++;
     if (printl_count == 3)
	  printl_count = 0;
}

void cfg_print_images (void)
{
     struct IMAGES *p;
     char *label, *alias;

     char *ret = cfg_get_strg_i (cf_options, "default");
     int defflag=0;

     printl_count = 0;
     for (p = images; p; p = p->next) {
	  if (p->obsolete)
		  continue;

	  label = cfg_get_strg_i (p->table, "label");
	  if (!label) {
	       label = cfg_get_strg_i (p->table, "image");
	       alias = strrchr (label, '/');
	       if (alias)
		    label = alias + 1;
	  }
	  if(!strcmp(bootoncelabel,label))
	       defflag=2;
	  else if(!strcmp(ret,label))
	       defflag=1;
	  else
	       defflag=0;
	  alias = cfg_get_strg_i (p->table, "alias");
	  printlabel (label, defflag);
	  if (alias)
	       printlabel (alias, 0);
     }
     prom_printf("\n");
}

char *cfg_get_default (void)
{
     char *label;
     struct IMAGES *p;
     char *ret = cfg_get_strg_i (cf_options, "default");

     if (ret)
	  return ret;
     if (!images)
	  return 0;

     for (p = images; p && p->obsolete; p = p->next);
     if (!p)
	     return 0;

     ret = cfg_get_strg_i (p->table, "label");
     if (!ret) {
	  ret = cfg_get_strg_i (p->table, "image");
	  label = strrchr (ret, '/');
	  if (label)
	       ret = label + 1;
     }
     return ret;
}

/*
 * cfg_set_default_by_mac ()
 * return 1 if the default cf_option was changed to label with the MAC addr
 * return 0 if not changed
 */
int cfg_set_default_by_mac (char *mac_addr)
{
     CONFIG *walk;
     struct IMAGES *tmp;
     char * label = NULL;
     int haslabel = 0;

     /* check if there is an image label equal to mac_addr */
     for (tmp = images; tmp; tmp = tmp->next) {
        label = cfg_get_strg_i (tmp->table, "label");
        if (!strcmp(label,mac_addr)){
            haslabel = 1;
        }
     }

     if (!haslabel)
         return 0;
     else {
         /*
          * if there is an image label equal to mac_addr, change the default
          * cf_options to this image label
          */
         for (walk = cf_options; walk->type != cft_end; walk++) {
             if (!strcasecmp(walk->name,"default")) {
                 walk->data = mac_addr;
                 return 1;
             }
         }
         return 0;
     }
}

/*
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
