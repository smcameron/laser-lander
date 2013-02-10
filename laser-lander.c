#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "snis_font.h"
#include "snis_typeface.h"
#include "joystick.h"
#include "libol.h"

#define SCREEN_WIDTH (1000.0)
#define SCREEN_HEIGHT (1000.0)
#define XSCALE (1.0 / (SCREEN_WIDTH / 2.0))
#define YSCALE (-1.0 / (SCREEN_HEIGHT / 2.0))
static float gravity = 0.1;

#define JOYSTICK_DEVICE "/dev/input/js0"
static int joystick_fd = -1;
static int camerax = 0;
static int cameray = 0;

struct object;

typedef void (*move_function)(struct object *o, float time);
typedef void (*draw_function)(struct object *o);

#define MAXOBJS 1000
#define NBITBLOCKS ((MAXOBJS >> 5) + 1)  /* 5, 2^5 = 32, 32 bits per int. */

struct my_point_t lander_points[] = {
	{ 0, -20 },
	{ 10, -15 },
	{ 10, 10 },
	{ 0, 16},
	{ 6, 20},
	{ -6, 20 },
	{ 0, 16},
	{ -10, 10},
	{ -10, -15 },
	{0, -20},
	{ LINE_BREAK, LINE_BREAK },
	{ -5, 10},
	{ -15, 25 },
	{ LINE_BREAK, LINE_BREAK },
	{ 5, 10},
	{ 15, 25 },
};
struct my_vect_obj lander_vect;

static struct object {
	float x, y, angle;
	float vx, vy;
	struct my_vect_obj *v;
	move_function move;
	draw_function draw;
} o[MAXOBJS];
static int nobjs = 1;
static unsigned int free_obj_bitmap[NBITBLOCKS] = { 0 };
static highest_object_number = 0;
static struct object *lander = &o[0];

#define NTERRAINPTS 1000
struct my_point_t terrain[NTERRAINPTS] = { 0 };

void draw_generic(struct object *o)
{
	int j;
	int x1, y1, x2, y2;

	if (o->v->p == NULL)
		return;

	x1 = o->x + o->v->p[0].x - camerax;
	y1 = o->y + o->v->p[0].y - cameray;  
	for (j = 0; j < o->v->npoints - 1; j++) {
		if (o->v->p[j+1].x == LINE_BREAK) { /* Break in the line segments. */
			j += 2;
			x1 = o->x + o->v->p[j].x - camerax;
			y1 = o->y + o->v->p[j].y - cameray;  
		}
		if (o->v->p[j].x == COLOR_CHANGE) {
			/* do something here to change colors */
			j += 1;
			x1 = o->x + o->v->p[j].x - camerax;
			y1 = o->y + o->v->p[j].y - cameray;  
		}
		x2 = o->x + o->v->p[j+1].x - camerax; 
		y2 = o->y + o->v->p[j+1].y - cameray;
		if (x1 > 0 && x2 > 0)
			olLine(x1, y1, x2, y2, C_WHITE); 
		x1 = x2;
		y1 = y2;
	}
}

static void generate_terrain(int first, int last)
{
	int midpoint;
	short midx, midy;
	short dx, dy;
	int dist;
	

	if (last - first <= 1)
		return;

	midpoint = (last - first) / 2 + first;
	dx = terrain[last].x - terrain[first].x;
	dy = terrain[last].y - terrain[first].y;
	midx = terrain[first].x + dx / 2;
	midy = terrain[first].y + dy / 2;
	dist = (int) sqrt(dx * dx + dy * dy);
	midy += (short) (((double) rand() / (double) RAND_MAX) * dist * 0.2);
	terrain[midpoint].x = midx;
	terrain[midpoint].y = midy;
	generate_terrain(first, midpoint);
	generate_terrain(midpoint, last);
}

static init_terrain(void)
{
	int first = 0;
	int last = NTERRAINPTS - 1;

	terrain[first].x = 0;
	terrain[first].y = 0;
	terrain[last].x = (short) (16 * SCREEN_WIDTH);
	terrain[last].y = 0;
	generate_terrain(first, last);
}

int find_free_obj()
{
	int i, j, answer;
	unsigned int block;

	/* this might be optimized by find_first_zero_bit, or whatever */
	/* it's called that's in the linux kernel.  But, it's pretty */
	/* fast as is, and this is portable without writing asm code. */
	/* Er, portable, except for assuming an int is 32 bits. */

	for (i = 0; i < NBITBLOCKS; i++) {
		if (free_obj_bitmap[i] == 0xffffffff) /* is this block full?  continue. */
			continue;

		/* I tried doing a preliminary binary search using bitmasks to figure */
		/* which byte in block contains a free slot so that the for loop only */
		/* compared 8 bits, rather than up to 32.  This resulted in a performance */
		/* drop, (according to the gprof) perhaps contrary to intuition.  My guess */
		/* is branch misprediction hurt performance in that case.  Just a guess though. */

		/* undoubtedly the best way to find the first empty bit in an array of ints */
		/* is some custom ASM code.  But, this is portable, and seems fast enough. */
		/* profile says we spend about 3.8% of time in here. */

		/* Not full. There is an empty slot in this block, find it. */
		block = free_obj_bitmap[i];
		for (j=0;j<32;j++) {
			if (block & 0x01) {	/* is bit j set? */
				block = block >> 1;
				continue;	/* try the next bit. */
			}

			/* Found free bit, bit j.  Set it, marking it non free.  */
			free_obj_bitmap[i] |= (1 << j);
			answer = (i * 32 + j);	/* return the corresponding array index, if in bounds. */
			if (answer < MAXOBJS) {
				if (answer > highest_object_number)
					highest_object_number = answer;
				return answer;
			}
			return -1;
		}
	}
	return -1;
}

static inline void free_object(int i)
{
        free_obj_bitmap[i >> 5] &= ~(1 << (i % 32)); /* clear the proper bit. */
}

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

static void move_generic(struct object *o, float elapsed_time)
{
#if 1
	o->x += o->vx * elapsed_time;
	o->y += o->vy * elapsed_time;
	o->vy += gravity;
#endif
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

static void draw_title_screen(void)
{
	static int y = 400;
	static int vy = 5;

	abs_xy_draw_string("LASER LANDER", BIG_FONT, 120, y);
	y += vy;
	if (y > 600)
		vy = -5;
	if (y < 300)
		vy = 5;
	abs_xy_draw_string("(c) Stephen M. Cameron 2013", SMALL_FONT, 250, 700); 
}

static int attract_mode_active = 1;

static void attract_mode(void)
{
	if (!attract_mode_active)
		return;
	draw_title_screen();
}

static void deal_with_joystick(void)
{
	static struct wwvi_js_event jse;
	int *xaxis, *yaxis, rc, i;

	if (joystick_fd < 0)
		return;

	xaxis = &jse.stick_x;
        yaxis = &jse.stick_y;

	memset(&jse.button[0], 0, sizeof(jse.button[0]*10));
	rc = get_joystick_status(&jse);
	if (rc != 0)
		return;

#define JOYSTICK_SENSITIVITY 5000
#define XJOYSTICK_THRESHOLD 20000

	/* check joystick buttons */

	for (i = 0; i < 11; i++) {
		if (jse.button[i] == 1) {
			attract_mode_active = 0;
		}
	}
}

static void setup_vects(void)
{
	setup_vect(lander_vect, lander_points);
}

static void move_camera(void)
{
	int dcx, dcy; /* desired camera position */

	dcx = camerax;
	dcy = cameray;

	if (lander->vx > 0)
		dcx = lander->x - 300;
	if (lander->vx < 0)
		dcx = lander->x - 700;
	if (lander->vy > 0)
		dcy = lander->y - 300;
	if (lander->vy < 0)
		dcy = lander->y - 700;
	camerax += (int) (0.05 * (dcx - camerax));
	cameray += (int) (0.05 * (dcy - cameray));
}

int main(int argc, char *argv[])
{
	float elapsed_time = 0.0;

	free_obj_bitmap[0] = 0x01;
	lander->x = 500;
	lander->y = 500;
	lander->vx = 0;
	lander->vy = 0;
	lander->v = &lander_vect;
	lander->draw = draw_generic;
	lander->move = move_generic;

	setup_vects();
	init_terrain();
	joystick_fd = open_joystick(JOYSTICK_DEVICE, NULL);
	if (joystick_fd < 0)
		printf("No joystick...");

	snis_typefaces_init();

	if (setup_openlase())
		return -1;

	while(1) {
		move_camera();
		deal_with_joystick();
		draw_objs();
		attract_mode();
		openlase_renderframe(&elapsed_time);
		move_objs(elapsed_time);
	}
	olShutdown();
	if (joystick_fd >= 0)
		close_joystick();
	return 0;
}

