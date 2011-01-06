/*
 *  nonstd.h - A collection of trivial wrappers to allow typical libraries
 *             to work within the yaboot environment.
 *
 *  Copyright 2011 Tony Breeds, IBM Corporation
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

#ifndef NONSTD_H
#define NONSTD_H

typedef int FILE;

extern FILE *stdout;

int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int fputs(const char *s, FILE *stream);
int fflush(FILE *stream);
char *getenv(const char *name);

#endif
