/*
 *  cmdline.c - Prompt handling
 *
 *  Copyright (C) 2001, 2002 Ethan Benson
 *
 *  Adapted from SILO
 *
 *  Copyright (C) 1996 Maurizio Plaza
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
#include "stdarg.h"
#include "prom.h"
#include "string.h"
#include "cfg.h"

#define CMD_LENG	512
char cbuff[CMD_LENG];
char passwdbuff[CMD_LENG];
extern int useconf;

void cmdinit()
{
    cbuff[0] = 0;
    passwdbuff[0] = 0;
}

void cmdedit (void (*tabfunc) (void), int password)
{
     int x, c;
     char *buff = password ? passwdbuff : cbuff;
     for (x = 0; x < CMD_LENG - 1; x++) {
	  if (buff[x] == 0)
	       break;
	  else if (password)
	       prom_printf("*");
     }
     if (!password)
	  prom_printf(buff, x);
     
     for (;;) {
	  c = prom_getchar ();
	  if (c == -1)
	       break;
	  if (c == '\n' || c == '\r') {
	       break;
	  }
	  if (c == '\t' && !x && tabfunc)
	       (*tabfunc) ();
	  if (c == '\b' || c == 0x7F) {
	       if (x > 0) {
		    --x;
		    buff[x] = 0;
		    prom_printf("\b \b");
	       }
	  } else if ((c & 0xE0) != 0) {
	       if (x < CMD_LENG - 1) {
		    buff[x] = c;
		    buff[x + 1] = 0;
		    if (password)
			 prom_printf("*");
		    else
			 prom_printf(buff + x);
		    x++;
	       }
	       if (x == 1 && !password && useconf) {
		    if (cfg_get_flag (cbuff, "single-key"))
			 break;
	       }
	  }
     }
     buff[x] = 0;
}
