/*
 *  errors.h - Definitions of error numbers returned by filesystems
 *
 *  Copyright (C) 2001  Ethan Benson
 *
 *  Copyright (C) 1999 Benjamin Herrenschmidt
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Simple error codes */
#define FILE_ERR_OK             0
#define FILE_ERR_EOF            -1
#define FILE_ERR_NOTFOUND       -2
#define FILE_CANT_SEEK          -3
#define FILE_IOERR              -4
#define FILE_BAD_PATH           -5
#define FILE_ERR_BAD_TYPE       -6
#define FILE_ERR_NOTDIR         -7
#define FILE_ERR_BAD_FSYS       -8
#define FILE_ERR_SYMLINK_LOOP   -9
#define FILE_ERR_LENGTH         -10
#define FILE_ERR_FSBUSY         -11
#define FILE_ERR_BADDEV         -12

/* Device kind */
#define FILE_DEVICE_BLOCK       1
#define FILE_DEVICE_NET         2
