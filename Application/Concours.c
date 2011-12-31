/* 
* Concours.c - Exécute l'interface graphique pour le concours 
* sur Mini2440
* 
* Copyright c 2011-2012 Théou Jean-Baptiste <jbtheou@gmail.com>
* 
* Based on ftfb.c - print FreeType rendered glyphs on the Linux Framebuffer
* Based on tslib_test - test for touchscreen calibration
* 
* Copyright Â© 2004-2005 Roger Leigh <rleigh at debian dot org>
*
* ftfb is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* ftfb is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston,
* MA 02111-1307 USA
*
* ******************************************/
#include "config.h"
#include "tslib.h"
#include "fbutils.h"
#include "testutils.h"
#include <unistd.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include "testutils.h"
#include <ft2build.h>
#include FT_FREETYPE_H



#define EVENT_NAME "/dev/input/event0"
#define RED	0xA0
#define FB_NAME "/dev/fb0"
#define ABOUT_SIZE_X 50
#define ABOUT_SIZE_Y 100


/*
 * Hack rapide pas très joli pour retirer la gestion des exceptions (unwind)
 *
 *
 */

void __libc_init_array(){}
void _Unwind_VRS_Get(){}
void _Unwind_VRS_Set(){}
void __aeabi_unwind_cpp_pr2(){}
void __aeabi_unwind_cpp_pr1(){}
void __aeabi_unwind_cpp_pr0(){}
void _Unwind_VRS_Pop(){}
void _Unwind_GetCFA(){}
void __gnu_Unwind_RaiseException(){}
void __gnu_Unwind_ForcedUnwind(){}
void __gnu_Unwind_Resume(){}
void __gnu_Unwind_Resume_or_Rethrow(){}
void _Unwind_Complete(){}
void _Unwind_DeleteException(){}
void __gnu_Unwind_Backtrace(){}
void __restore_core_regs(){}
void restore_core_regs(){}
void __gnu_Unwind_Restore_VFP(){}
void __gnu_Unwind_Save_VFP(){}
void __gnu_Unwind_Restore_VFP_D(){}
void __gnu_Unwind_Save_VFP_D(){}
void __gnu_Unwind_Restore_VFP_D_16_to_31(){}
void __gnu_Unwind_Save_VFP_D_16_to_31(){}
void __gnu_Unwind_Restore_WMMXD(){}
void __gnu_Unwind_Save_WMMXD(){}
void __gnu_Unwind_Restore_WMMXC(){}
void __gnu_Unwind_Save_WMMXC(){}
void ___Unwind_RaiseException(){}
void _Unwind_RaiseException(){}
void _Unwind_Resume(){}
void ___Unwind_Resume(){}
void _Unwind_Resume_or_Rethrow(){}
void ___Unwind_Resume_or_Rethrow(){}
void _Unwind_ForcedUnwind(){}
void ___Unwind_ForcedUnwind(){}
void ___Unwind_Backtrace(){}
void _Unwind_Backtrace(){}


/* ********************** */

FT_Library ft_lib;
FT_Face default_face;

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
void draw_pixel(unsigned int x, unsigned int y, unsigned int col);
void my_draw_bitmap(FT_Bitmap* src, unsigned int color, int xo, int yo, char type);
int fbfd = 0;
char *fbp = 0;
struct tsdev *ts;

typedef struct {
	int x[5], xfb[5];
	int y[5], yfb[5];
	int a[7];
} calibration;

long int screensize = 0;

FT_Matrix matrix; /* transformation matrix */

inline static int sort_by_x(const void* a, const void *b)
{
	return (((struct ts_sample *)a)->x - ((struct ts_sample *)b)->x);
}

inline static int sort_by_y(const void* a, const void *b)
{
	return (((struct ts_sample *)a)->y - ((struct ts_sample *)b)->y);
}


void getxy(struct tsdev *ts, int *x, int *y)
{
#define MAX_SAMPLES 32
	struct ts_sample samp[MAX_SAMPLES];
	int index, middle;
	do{
		ts_read_raw(ts, &samp[0], 1);
	} while (samp[0].pressure == 0);

	/* Now collect up to MAX_SAMPLES touches into the samp array. */
	index = 0;
	do {
		if (index < MAX_SAMPLES-1)
			index++;
		ts_read_raw(ts, &samp[index], 1);
	} while (samp[index].pressure > 0);

	/*
	 * At this point, we have samples in indices zero to (index-1)
	 * which means that we have (index) number of samples.  We want
	 * to calculate the median of the samples so that wild outliers
	 * don't skew the result.  First off, let's assume that arrays
	 * are one-based instead of zero-based.  If this were the case
	 * and index was odd, we would need sample number ((index+1)/2)
	 * of a sorted array; if index was even, we would need the
	 * average of sample number (index/2) and sample number
	 * ((index/2)+1).  To turn this into something useful for the
	 * real world, we just need to subtract one off of the sample
	 * numbers.  So for when index is odd, we need sample number
	 * (((index+1)/2)-1).  Due to integer division truncation, we
	 * can simplify this to just (index/2).  When index is even, we
	 * need the average of sample number ((index/2)-1) and sample
	 * number (index/2).  Calculate (index/2) now and we'll handle
	 * the even odd stuff after we sort.
	 */
	middle = index/2;
	if (x) {
		qsort(samp, index, sizeof(struct ts_sample), sort_by_x);
		if (index & 1)
			*x = samp[middle].x;
		else
			*x = (samp[middle-1].x + samp[middle].x) / 2;
	}
	if (y) {
		qsort(samp, index, sizeof(struct ts_sample), sort_by_y);
		if (index & 1)
			*y = samp[middle].y;
		else
			*y = (samp[middle-1].y + samp[middle].y) / 2;
	}
}

int open_device(char* str){
	// On ouvre le framebuffer
	fbfd = open(str, O_RDWR);
   	// On récupère les information sur le frame buffer
	ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo);
	ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo);
   	// On calcule la taille de l'écran 
	screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;
   	// On map la mémoire du framebuffer
	fbp = (char *)mmap(0,screensize ,PROT_READ | PROT_WRITE,MAP_SHARED,fbfd, 0);
	// On met le fond de l'écran en rouge
	memset(fbp,RED,screensize);
   	// On ouvre le touchscreen
	ts = ts_open(EVENT_NAME,0);
  	// On configure le touchscreen
	ts_config(ts);
	return 0;
}


void init_freetype(char * police){
	FT_Init_FreeType(&ft_lib);
	FT_New_Face( ft_lib, police, 0, &default_face);
	matrix.xx = (FT_Fixed)( 0x10000L );
	matrix.xy = (FT_Fixed)( 0 );
	matrix.yx = (FT_Fixed)( 0 );
	matrix.yy = (FT_Fixed)( 0x10000L );
}

//x y are virtual coords
void draw_string(char* text, unsigned int col, char size, int x, int y){
	
	FT_Vector pen; /* untransformed origin */
	int pen_x, pen_y, n;
	int num_chars = strlen(text);
	pen_x = x;
	pen_y = y;
	FT_GlyphSlot slot = default_face->glyph;
	pen.x = pen_x * 64;
	pen.y = ( size - pen_y ) * 64;
	FT_Set_Pixel_Sizes( default_face, 0, size); // set font size 
	for ( n = 0; n < num_chars; n++ )
	{
		FT_Set_Transform( default_face, &matrix, &pen );
		FT_Load_Char( default_face, text[n], FT_LOAD_RENDER );
		my_draw_bitmap( &slot->bitmap, col, slot->bitmap_left, size - slot->bitmap_top, 1);
		pen.x += slot->advance.x;
		pen.y += slot->advance.y;
	} 
}

//xo and yo are real coords
void my_draw_bitmap(FT_Bitmap* src, unsigned int color, int xo, int yo, char type){
	int xi, yi;
	int offset;		
	int x, y;
	unsigned int col, ncol;
	unsigned int location;

	char red = (color & 0x00ff0000) >> 16;
	char green = (color & 0x0000ff00) >> 8;
	char blue = (color & 0x000000ff);

	char old_red, old_green, old_blue;
	for(yi = 0; yi < src->rows; yi++){
		for(xi = 0; xi < src->width; xi++){
			if(type == 1){
				offset = (yi * src->pitch) + xi;
				col = src->buffer[offset];
			}
			else if(type == 2){
				col = color;
			}
			ncol = 255 - col;
			x = xo + xi;
			y = yo + yi;
			
			location = (x + vinfo.xoffset) * (vinfo.bits_per_pixel / 8) + (y + vinfo.yoffset) * finfo.line_length;
			if(vinfo.bits_per_pixel == 16){
				if(col > 253){
					//write without transparency
					*((char*)fbp + location + 1) = (red & 0xf8) | ((green >> 5) & 0x07);
					*((char*)fbp + location) = ((green << 3) & 0xe0) | ((blue >> 3) & 0x1f);
				}
				else if(col == 0){
					//dont' change it
				}
				else{
					old_red    = (*((char*)fbp + location + 1)) & 0xf8;
                    old_green  = (*((char*)fbp + location + 1) & 0x07) << 5;
                    old_green |= (*((char*)fbp + location) & 0xe0) >> 3;
                    old_blue   = (*((char*)fbp + location) & 0x1f) << 5;

                    old_red = (char)((((short)red * col) + ((short)old_red * ncol)) / 255);
                    old_green = (char)((((short)green * col) + ((short)old_green * ncol)) / 255);
                    old_blue = (char)((((short)blue * col) + ((short)old_blue * ncol)) / 255);
                                  
                    *((char*)fbp + location + 1) = (old_red & 0xf8) | ((old_green >> 5) & 0x07);
                    *((char*)fbp + location) = ((old_green << 3) & 0xe0) | ((old_blue >> 3) & 0x1f);
				}
		
			}
		}
	}	
}

int main(int argc, char * argv[]){
	//printf("Application user %s %s %s\r\n", argv[1], argv[2], argv[3]);
	char acces = 0;
	int size;
	sscanf(argv[2],"%d",&size);
	open_device(FB_NAME);
	FT_Bitmap monbitmap;
	monbitmap.rows=ABOUT_SIZE_X;
	monbitmap.width=ABOUT_SIZE_Y;
	init_freetype(argv[3]);
	draw_string(argv[1],0,size, vinfo.xres/2-strlen(argv[1])*size/4, vinfo.yres/2);
	my_draw_bitmap(&monbitmap, 0xFF, vinfo.xres-100,vinfo.yres-50, 2);
	draw_string("A propos",0, 12, vinfo.xres-80, vinfo.yres-20);
	calibration cal;
	while(1){
		getxy (ts, &cal.x [0], &cal.y [0]);
		//printf("X = %4d Y = %4d\n", cal.x [0], cal.y [0]);
		if(cal.x [0] < 900 && cal.x [0] > 750 && cal.y [0] < 340 && cal.y [0] > 90)
		{
			memset(fbp,0xA0,screensize);
			my_draw_bitmap(&monbitmap, 0xFF, vinfo.xres-100,vinfo.yres-50, 2);

			if(acces == 0){				
				draw_string("Jean-Baptiste Theou",0, 14, vinfo.xres/2-19*15/4, vinfo.yres/2);
				draw_string("Retour",0,12, vinfo.xres-80, vinfo.yres-20);
				acces = 1;
			}
			else if(acces == 1){
				draw_string(argv[1],0,size, vinfo.xres/2-strlen(argv[1])*size/4, vinfo.yres/2);
				draw_string("A propos",0, 13, vinfo.xres-80, vinfo.yres-20);
				acces = 0;
			}
		}
	}	
	return 1;
}
