#include "prom.h"

#define WINDOW_X_SIZE 640
#define WINDOW_Y_SIZE 480


static prom_handle videodev;
static prom_handle videop;
static int Xres, Yres;
static int Xstart, Ystart;
static int rowbytes;
static int zoom;
static unsigned char *address;

static int scrSetColorMap( unsigned char color,
	unsigned char r, unsigned char g, unsigned char b );


int scrOpen()
{
  int result = 0;
 
  videodev = (prom_handle)call_prom( "open", 1, 1, "screen" );
  if( videodev == PROM_INVALID_HANDLE )
     return(-1);
  videop = (prom_handle)call_prom( "instance-to-package", 1, 1, videodev );
  if( videop == PROM_INVALID_HANDLE )
     return(-1);

  result |= prom_getprop(videop, "width", &Xres, 4 );
  result |= prom_getprop(videop, "height", &Yres, 4 );
  result |= prom_getprop(videop, "address", &address, 4 );
  result |= prom_getprop(videop, "linebytes", &rowbytes, 4 );

  prom_map (address, address, rowbytes * Xres);

#if DEBUG
  prom_printf("width     : %d\n", Xres);
  prom_printf("height    : %d\n", Yres);
  prom_printf("address   : 0x%08lx\n", address);
  prom_printf("linebytes : %d\n", rowbytes);
  prom_printf("result    : %d\n", result);
#endif
 
  if( result < 0 )
     return( -1 );

  zoom = Xres / WINDOW_X_SIZE > Yres / WINDOW_Y_SIZE ? Yres / WINDOW_Y_SIZE : Xres / WINDOW_X_SIZE;

  Xstart = Xres / 2 - WINDOW_X_SIZE / 2 * zoom;
  Ystart = Yres / 2 - WINDOW_Y_SIZE / 2 * zoom;

#if DEBUG
  prom_printf("zoom      : %d\n", zoom);
  prom_printf("Xstart    : %d\n", Xstart);
  prom_printf("Ystart    : %d\n", Ystart);
#endif

  return( 0 );
}


void scrClear( unsigned char c )
{
  int x, y;

  for (y = 0; y < Yres; y++)
  	for (x = 0; x < Xres; x++)
  		address[y * rowbytes + x] = c;
}


void scrClose()
{
  call_prom( "close", 1, 0, videodev );
  videodev = 0;
}


void scrReset()
{
  int c;
  extern unsigned char color_table_red[],
                       color_table_green[],
                       color_table_blue[];

  for( c = 0; c < 16; ++c )
     scrSetColorMap( c, color_table_red[c],
                        color_table_green[c],
                        color_table_blue[c] );
//  scrClose();
}

static void inline do_pix( int x, int y, unsigned char c )
{
  address[ y * rowbytes + x ] = c;
}


void scrPutPixel( int x, int y, unsigned char c )
{
  int zx, zy;

  for( zy = 0; zy < zoom; ++zy )
     for( zx = 0; zx < zoom; ++zx )
        do_pix( x * zoom + zx + Xstart, y * zoom + zy + Ystart, c );
}


int scrSetColorMap( unsigned char color, unsigned char r, unsigned char g, unsigned char b )
{
  int result;

  result = (int)call_prom( "call-method", 6, 1, "color!", videodev, color, b, g, r );

  return( result );
}


void scrFillColorMap( unsigned char r, unsigned char g, unsigned char b )
{
  int c;

  for( c = 0; c < 256; ++c )
     scrSetColorMap( c, r, g, b );
}


void scrSetEntireColorMap( unsigned char *map )
{
  int c;

  for( c = 0; c < 256; ++c )
     scrSetColorMap( c, map[c * 3], map[c * 3 + 1], map[c * 3 + 2] );
}


void scrFadeColorMap( unsigned char *first, unsigned char *last, int rate )
{
  int inc, c;

  for( inc = 0; inc < 256; inc += rate )
     for( c = 0; c < 256; ++c )
        scrSetColorMap( c, first[c * 3 + 0] * (255 - inc) / 255 + last[c * 3 + 0] * inc / 255,
                           first[c * 3 + 1] * (255 - inc) / 255 + last[c * 3 + 1] * inc / 255,
                           first[c * 3 + 2] * (255 - inc) / 255 + last[c * 3 + 2] * inc / 255 );
}
