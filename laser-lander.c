#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "snis_font.h"
#include "snis_typeface.h"
#include "libol.h"

#define XDIM (1000.0)
#define YDIM (1000.0)
#define XSCALE (1.0 / (XDIM / 2.0))
#define YSCALE (-1.0 / (YDIM / 2.0))

struct object;

typedef void (*move_function)(struct object *o, float time);
typedef void (*draw_function)(struct object *o);

#define MAXOBJS 1000

static struct object {
	float x, y, angle;
	float vx, vy;
	move_function move;
	draw_function draw;
} o[MAXOBJS];
static int nobjs = 0;


/* Draws a letter in the given font at an absolute x,y coords on the screen. */
static int abs_xy_draw_letter(struct my_vect_obj **font, 
		unsigned char letter, int x, int y)
{
	int i, x1, y1, x2, y2;
	int minx, maxx, diff;

	if (letter == ' ' || letter == '\n' || letter == '\t' || font[letter] == NULL)
		return abs(font['Z']->p[0].x - font['Z']->p[1].x);

	for (i = 0; i < font[letter]->npoints-1; i++) {
		if (font[letter]->p[i+1].x == LINE_BREAK)
			i += 2;
		x1 = x + font[letter]->p[i].x;
		y1 = y + font[letter]->p[i].y;
		x2 = x + font[letter]->p[i + 1].x;
		y2 = y + font[letter]->p[i + 1].y;

		if (i == 0) {
			minx = x1;
			maxx = x1;
		}

		if (x1 < minx)
			minx = x1;
		if (x2 < minx)
			minx = x2;
		if (x1 > maxx)
			maxx = x1;
		if (x2 > maxx)
			maxx = x2;
		
		if (x1 > 0 && x2 > 0)
			olLine(x1, y1, x2, y2, C_WHITE); 
	}
	diff = abs(maxx - minx);
	/* if (diff == 0)
		return (abs(font['Z']->p[0].x - font['Z']->p[1].x) / 4); */
	return diff; 
}

/* Draws a string at an absolute x,y position on the screen. */ 
static void abs_xy_draw_string(char *s, int font, int x, int y) 
{

	int i, dx;	
	int deltax = 0;

	for (i=0;s[i];i++) {
		dx = (font_scale[font]) + abs_xy_draw_letter(gamefont[font], s[i], x + deltax, y);  
		deltax += dx;
	}
}

static void draw_objs(void)
{
	int i;

	for (i = 0; i < nobjs; i++)
		o[i].draw(&o[i]);
}

static void move_objs(float elapsed_time)
{
	int i;

	for (i = 0; i < nobjs; i++)
		o[i].move(&o[i], elapsed_time);
}

static int setup_openlase(void)
{
	OLRenderParams params;

	memset(&params, 0, sizeof params);
	params.rate = 48000;
	params.on_speed = 2.0/100.0;
	params.off_speed = 2.0/20.0;
	params.start_wait = 12;
	params.start_dwell = 3;
	params.curve_dwell = 0;
	params.corner_dwell = 12;
	params.curve_angle = cosf(30.0*(M_PI/180.0)); // 30 deg
	params.end_dwell = 3;
	params.end_wait = 10;
	params.snap = 1/100000.0;
	params.render_flags = RENDER_GRAYSCALE;

	if (olInit(3, 60000) < 0) {
		fprintf(stderr, "Failed to initialized openlase\n");
		return -1;
	}
	olSetRenderParams(&params);

	olLoadIdentity();
	olTranslate(-1,1);
	olScale(XSCALE, YSCALE);
	/* window is now 0,0 - 1,1 with y increasing down, x increasing right */

	return 0;
}

static void openlase_renderframe(float *elapsed_time)
{
	*elapsed_time = olRenderFrame(60);
	olLoadIdentity();
	olTranslate(-1,1);
	olScale(XSCALE, YSCALE);
	/* window is now 0,0 - 1,1 with y increasing down, x increasing right */
}

int main(int argc, char *argv[])
{
	float elapsed_time = 0.0;

	snis_typefaces_init();

	if (setup_openlase())
		return -1;

	while(1) {
		draw_objs();
		abs_xy_draw_string("LASER LANDER", BIG_FONT, 120, 500);
		openlase_renderframe(&elapsed_time);
		move_objs(elapsed_time);
	}
	olShutdown();
	return 0;
}

