/*
 *  Debug defines
 *
 *  Copyright (C) 2001  Ethan Benson
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

#if DEBUG
# define DEBUG_ENTER prom_printf( "--> %s\n", __PRETTY_FUNCTION__ )
# define DEBUG_LEAVE(str) \
    prom_printf( "<-- %s - %s\n", __PRETTY_FUNCTION__, #str )
# define DEBUG_LEAVE_F(args...)\
{\
    prom_printf( "<-- %s - %d\n", __PRETTY_FUNCTION__, ## args );\
}
# define DEBUG_F(fmt, args...)\
{\
    prom_printf( "    %s - ", __PRETTY_FUNCTION__ );\
    prom_printf( fmt, ## args );\
}
# define DEBUG_OPEN DEBUG_F( "dev=%s, part=0x%p (%d), file_name=%s\n",\
                             dev_name, part, part ? part->part_number : -1,\
                             file_name)
# define DEBUG_SLEEP prom_sleep(3)
#else
#define DEBUG_ENTER
#define DEBUG_LEAVE(x)
#define DEBUG_LEAVE_F(args...)
#define DEBUG_F(fmt, args...)
#define DEBUG_OPEN
#define DEBUG_SLEEP
#endif

/* 
 * Local variables:
 * c-file-style: "k&r"
 * c-basic-offset: 5
 * End:
 */
