/*
 *  linux/fs/isofs/util.c
 *
 *  The special functions in the file are numbered according to the section
 *  of the iso 9660 standard in which they are described.  isonum_733 will
 *  convert numbers according to section 7.3.3, etc.
 *
 *  isofs special functions.  This file was lifted in its entirety from
 *  the 386BSD iso9660 filesystem, by Pace Willisson <pace@blitz.com>.
 */

int
isonum_711 (char * p)
{
     return (*p & 0xff);
}

int
isonum_712 (char * p)
{
     int val;
	
     val = *p;
     if (val & 0x80)
	  val |= 0xffffff00;
     return (val);
}

int
isonum_721 (char * p)
{
     return ((p[0] & 0xff) | ((p[1] & 0xff) << 8));
}

int
isonum_722 (char * p)
{
     return (((p[0] & 0xff) << 8) | (p[1] & 0xff));
}

int
isonum_723 (char * p)
{
#if 0
     if (p[0] != p[3] || p[1] != p[2]) {
	  fprintf (stderr, "invalid format 7.2.3 number\n");
	  exit (1);
     }
#endif
     return (isonum_721 (p));
}

int
isonum_731 (char * p)
{
     return ((p[0] & 0xff)
	     | ((p[1] & 0xff) << 8)
	     | ((p[2] & 0xff) << 16)
	     | ((p[3] & 0xff) << 24));
}

int
isonum_732 (char * p)
{
     return (((p[0] & 0xff) << 24)
	     | ((p[1] & 0xff) << 16)
	     | ((p[2] & 0xff) << 8)
	     | (p[3] & 0xff));
}

int
isonum_733 (char * p)
{
#if 0
     int i;

     for (i = 0; i < 4; i++) {
	  if (p[i] != p[7-i]) {
	       fprintf (stderr, "bad format 7.3.3 number\n");
	       exit (1);
	  }
     }
#endif
     return (isonum_731 (p));
}

/* 
 * Local variables:
 * c-file-style: "K&R"
 * c-basic-offset: 8
 * End:
 */
