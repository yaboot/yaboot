/* Crude PCX file loading and display for 640x480 image at boot */

unsigned char *pcxColormap(unsigned char *gfx_file, int gfx_len)
{
  return(gfx_file + gfx_len - 768);
}

extern void scrPutPixel( int x, int y, unsigned char c );

static void inline do_putpixel(int offset, unsigned char c)
{
   scrPutPixel(offset % 640, offset / 640, c);
}

void pcxDisplay(unsigned char *gfx_file, unsigned gfx_len)
{
  int offset, file_offset, i, c, f;

  for(offset = 0, file_offset = 128; offset < 640 * 480 && file_offset < gfx_len - 769; file_offset++)
  {
     if(((c = gfx_file[file_offset]) & 0xc0) == 0xc0)
     {
        f = gfx_file[++file_offset];
        c &= 0x3f;
        for(i = 0; i < c; ++i)
           do_putpixel(offset++, f);
     }
     else
        do_putpixel(offset++, c);
  }
}
