/* effects.c */
/* Adds splash screen and status bar effects for graphical yaboot */

#include "file.h"
#include "yaboot.h"
#include "stdlib.h"

#define SB_XSTART  170
#define SB_XEND    472
#define SB_YSTART  402

unsigned char bar_grad[] = { 228, 237, 246, 254, 207, 254, 246, 237, 228, 206 };

int scrOpen(void);
void scrSetEntireColorMap(unsigned char *);
void scrClear(unsigned char);
void pcxDisplay(unsigned char *, unsigned int);
void scrFadeColorMap(unsigned char *, unsigned char *, int);
unsigned char *pcxColormap(unsigned char *, int);
void scrPutPixel(int, int, unsigned char);

void fxDisplaySplash(struct boot_fspec_t *filespec)
{
  void              *gfx_file;
  int                gfx_len;
  unsigned char     *grey_map;
  int                start_time;
  struct boot_file_t file;
  int                result;
  static int         been_here = 0;

  if(!been_here)
  {
     been_here = 1;
     gfx_file = (void *) malloc(1024 * 512);
     if (gfx_file == NULL) {
	prom_printf("malloc failed in fxDisplaySplash()\n");
	return;
     }

     scrOpen();

     result = open_file(filespec, &file);

     if(result != FILE_ERR_OK)
     {
        prom_printf("Error loading splash screen\n");
        return;
     }

     gfx_len = file.fs->read(&file, 1024 * 512 - 765, gfx_file);
     file.fs->close(&file);
     grey_map = gfx_file + gfx_len;
     memset(grey_map, 0/*128*/, 765);
  }

  scrSetEntireColorMap(grey_map);
  scrClear(0);

  start_time = prom_getms();
  while(prom_getms() < start_time + 2000);

  pcxDisplay(gfx_file, gfx_len);
  scrFadeColorMap(grey_map, pcxColormap( gfx_file, gfx_len ), 2);
}


void fxUpdateSB(unsigned number, unsigned total)
{
  int x, y;

  for(x = SB_XSTART; x < SB_XSTART + (SB_XEND - SB_XSTART) * number / total; ++x)
  {
     for(y = 0; y < 10; ++y)
        scrPutPixel(x, SB_YSTART + y, bar_grad[y]);
  }
}


#define BLOCK_INDEX (1024 * 64)

int fxReadImage(struct boot_file_t *file, unsigned int filesize, void *base)
{
  unsigned int count, result = 0;

  for(count = 0; count < filesize; count += result)
  {
     result = ((filesize - count) < BLOCK_INDEX) ? (filesize - count) : BLOCK_INDEX;
     if ((result = file->fs->read(file, result, base + count)) != BLOCK_INDEX)
     	break;
     fxUpdateSB(count + result, filesize);
  }
  fxUpdateSB(count + result, filesize);
  return(count + result);
}
