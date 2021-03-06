#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#include "snis_font.h"
#include "snis_typeface.h"
#include "joystick.h"
#include "libol.h"

#define SCREEN_WIDTH (1000.0)
#define SCREEN_HEIGHT (1000.0)
#define XSCALE (1.0 / (SCREEN_WIDTH / 2.0))
#define YSCALE (-1.0 / (SCREEN_HEIGHT / 2.0))
#define TERRAIN_WIDTH (SCREEN_WIDTH * 16)
static float gravity = 0.3;
static float requested_thrust = 0.0;
static float requested_left = 0.0;
static float requested_right = 0.0;
static float fuel = 1000.0;
static int crash_screen = 0;
static int successful_landing = 0;

#define JOYSTICK_DEVICE "/dev/input/js0"
static int joystick_fd = -1;
static int camerax = 0;
static int cameray = 0;
static int openlase_color = 0;
static int attract_mode_active = 1;
static int landing_evaluated = 0;

struct object;

#define GROUNDCOLOR 0xff7f00
#define NSPARKCOLORS 15
#define FLAMECOLOR (NSPARKCOLORS / 2)
int sparkcolor[NSPARKCOLORS];
#define SPARKLIFE NSPARKCOLORS
#define NRAINBOWSTEPS (256 / 3) 
#define NRAINBOWCOLORS (NRAINBOWSTEPS * 3)
int rainbow_color[NRAINBOWCOLORS];

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

struct my_point_t wreckage_points[] = {
	{ 0, 0 },
	{ 10, 4},
	{ 12, 7},
	{-13, 8},
	{-10, 9},
	{ LINE_BREAK, LINE_BREAK },
	{ -5, 10},
	{ -15, 25 },
	{ LINE_BREAK, LINE_BREAK },
	{ 5, 10},
	{ 15, 25 },
};
struct my_vect_obj wreckage_vect;

static struct object {
	float x, y, angle;
	float vx, vy;
	struct my_vect_obj *v;
	int alive, n;
	move_function move;
	draw_function draw;
} o[MAXOBJS];
static int nobjs = 1;
static unsigned int free_obj_bitmap[NBITBLOCKS] = { 0 };
static highest_object_number = 0;
static struct object *lander = &o[0];

#define NTERRAINPTS 100
struct my_point_t terrain[NTERRAINPTS] = { 0 };

#define CHECK_ALTITUDE 0
#define CHECK_HVELOCITY 1
#define CHECK_VVELOCITY 2
#define EXCELLENT_LANDING 3
#define NOT_BAD 4
#define ROOKIE 5
#define STAR_SPANGLED_BANNER 6
#define TERRIBLE 7
#define GO_FOR_LANDING 8
#define THRUSTER 9
#define EXPLOSION 10 
#define NUMSOUNDS 11
struct timeval last_sound_time;

void read_audio_data()
{
	wwviaudio_read_ogg_clip(CHECK_ALTITUDE, "check-altitude.ogg");
	wwviaudio_read_ogg_clip(CHECK_HVELOCITY, "check-hvelocity.ogg");
	wwviaudio_read_ogg_clip(CHECK_VVELOCITY, "check-vvelocity.ogg");
	wwviaudio_read_ogg_clip(EXCELLENT_LANDING, "excellent-landing.ogg");
	wwviaudio_read_ogg_clip(NOT_BAD, "not-bad.ogg");
	wwviaudio_read_ogg_clip(ROOKIE, "rookie-landing.ogg");
	wwviaudio_read_ogg_clip(STAR_SPANGLED_BANNER, "star-spangled-banner.ogg");
	wwviaudio_read_ogg_clip(TERRIBLE, "terrible.ogg");
	wwviaudio_read_ogg_clip(GO_FOR_LANDING, "go-for-landing.ogg");
	wwviaudio_read_ogg_clip(THRUSTER, "thruster.ogg");
	wwviaudio_read_ogg_clip(EXPLOSION, "crash-explosion.ogg");
}

void add_sound(int sound)
{
	struct timeval t;

	gettimeofday(&t, NULL);
#define SOUND_TIME_LIMIT 5 /* seconds */
	if ((t.tv_sec - last_sound_time.tv_sec) < SOUND_TIME_LIMIT)
		return;
	last_sound_time = t;
	wwviaudio_add_sound(sound);
}

void play_thruster_sound(void)
{
	static struct timeval t = {0};
	struct timeval now;

	gettimeofday(&now, NULL);
	if (now.tv_sec != t.tv_sec || (now.tv_usec - t.tv_usec > 300000)) {
		t = now;
		wwviaudio_add_sound(THRUSTER);
	}
}

void draw_generic(struct object *o)
{
	int j;
	int x1, y1, x2, y2;

	if (o->v->p == NULL)
		return;

	x1 = o->x + o->v->p[0].x - camerax;
	y1 = o->y + o->v->p[0].y - cameray;  

	olBegin(OL_LINESTRIP);
	olVertex(x1,y1,openlase_color);

	for (j = 0; j < o->v->npoints - 1; j++) {
		if (o->v->p[j+1].x == LINE_BREAK) { /* Break in the line segments. */
			j += 2;
			x1 = o->x + o->v->p[j].x - camerax;
			y1 = o->y + o->v->p[j].y - cameray;  
			olVertex(x1,y1,C_BLACK);
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
			olVertex(x2,y2,openlase_color);
		x1 = x2;
		y1 = y2;
	}
	olEnd();
}

void draw_landing_pad(struct object *o)
{
	static int rbcolor = 0;

	olLine(o->x - 20 - camerax, o->y - cameray,
			o->x + 20 - camerax, o->y - cameray,
			rainbow_color[rbcolor]);
	rbcolor = (rbcolor + 1) % NRAINBOWCOLORS;
}

void no_move(__attribute__ ((unused)) struct object *o,
		__attribute__ ((unused)) float elapsed_time)
{
	return;
}

void draw_lander(struct object *o)
{
	openlase_color = C_GREEN;
	draw_generic(o);

	/* draw flames */
	if (requested_thrust > 0.0 && fuel > 0.0) {
		olLine(o->x - 5 - camerax, o->y + 20 - cameray, 
			o->x - camerax, o->y + 65 - cameray, sparkcolor[FLAMECOLOR]);
		olLine(o->x + 5 - camerax, o->y + 20 - cameray, 
			o->x - camerax, o->y + 65 - cameray, sparkcolor[FLAMECOLOR]);
	}
	if (requested_right > 0.0 && fuel > 0.0) {
		olLine(o->x - 15 - camerax, o->y - cameray - 5, 
			o->x - 40 - camerax, o->y- cameray, sparkcolor[FLAMECOLOR]);
		olLine(o->x - 15 - camerax, o->y - cameray + 5, 
			o->x - 40 - camerax, o->y- cameray, sparkcolor[FLAMECOLOR]);
	}
	if (requested_left > 0.0 && fuel > 0.0) {
		olLine(o->x + 15 - camerax, o->y - cameray - 5, 
			o->x + 40 - camerax, o->y- cameray, sparkcolor[FLAMECOLOR]);
		olLine(o->x + 15 - camerax, o->y - cameray + 5, 
			o->x + 40 - camerax, o->y- cameray, sparkcolor[FLAMECOLOR]);
	}
}

static void draw_spark(struct object *o)
{
	if (o->alive < 0)
		return;
	if (o->alive >= NSPARKCOLORS)
		return;
	olDot(o->x - camerax, o->y - cameray, 2, sparkcolor[o->alive]);
}

static inline void free_object(int i);
static void move_generic(struct object *o, float elapsed_time);
static void move_spark(struct object *o, float elapsed_time)
{
	move_generic(o, elapsed_time);
	o->alive--;
	if (!o->alive)
		free_object(o->n);
}

static int add_spark(float x, float y, float vx, float vy, int life)
{
	int i = find_free_obj();
	struct object *s;

	if (i < 0)
		return;
	s = &o[i];
	s->n = i;
	s->x = x;
	s->y = y;
	s->vx = vx;
	s->vy = vy;
	s->draw = draw_spark;
	s->move = move_spark;
	s->v = NULL;
	s->alive = life;
}

static void exhaust(float x, float y, float vx, float vy,
			int amount, int life)
{
	int i;
	float tvx, tvy;

	for (i = 0; i < amount; i++) {
		tvx = vx + ((float) rand() / (float) RAND_MAX) * 100 - 50;
		tvy = vy + ((float) rand() / (float) RAND_MAX) * 100 - 50;
		add_spark(x, y, tvx, tvy, life); 
	}
}

static void explode(float x, float y, int amount, int life)
{
	int i;
	float tvx, tvy;

	for (i = 0; i < amount; i++) {
		tvx = ((float) rand() / (float) RAND_MAX) * 1000 - 500;
		tvy = ((float) rand() / (float) RAND_MAX) * 1000 - 500;
		add_spark(x, y, tvx, tvy, life); 
	}
}

static char *evaluate_landing(void)
{
	int i;
	int first = 1;
	float min_dist = 1e06;
	float dist;

	for (i = 0; i < MAXOBJS; i++) {
		if (o[i].alive && o[i].draw == draw_landing_pad) {
			float dx, dy;
			dx = o[i].x - lander->x;
			dy = o[i].y - lander->y;
			dist = (float) sqrt(dx * dx + dy * dy);
			if (first || dist < min_dist) {
				first = 0;
				min_dist = dist;
			}
		}
	}
	if (first) {
		return "Huh?";
	}
	if (min_dist < 28.0) {
		if (!landing_evaluated) {
			wwviaudio_add_sound(STAR_SPANGLED_BANNER);
			add_sound(EXCELLENT_LANDING);
			landing_evaluated = 1;
		}
		return "EXCELLENT";
	}
	if (min_dist < 40.0) {
		if (!landing_evaluated) {
			add_sound(NOT_BAD);
			landing_evaluated = 1;
		}
		return "NOT BAD";
	}
	if (min_dist < 60.0) {
		if (!landing_evaluated) {
			add_sound(ROOKIE);
			landing_evaluated = 1;
		}
		return "ROOKIE";
	}
	if (min_dist < 100.0) {
		if (!landing_evaluated) {
			add_sound(ROOKIE);
			landing_evaluated = 1;
		}
		return "POOR";
	}
	if (!landing_evaluated) {
		add_sound(TERRIBLE);
		landing_evaluated = 1;
	}
	return "TERRIBLE";
}

static void collision(void)
{
	if (crash_screen)
		return;

	if (lander->vy < 15.0 && fabs(lander->vx) < 10.0) {
		successful_landing = 1;
	} else {
		explode(lander->x, lander->y, 100, SPARKLIFE);
		lander->v = &wreckage_vect;
		successful_landing = 0;
		wwviaudio_add_sound(EXPLOSION);
	}
	crash_screen = 1;
}

static void draw_terrain(void)
{
	int i;
	double dist;
	int yintersect;
	int dx, dy;

	int first = 1;
	for (i = 0; i < NTERRAINPTS - 1; i++) {
		if ( first ) {
			olBegin(OL_LINESTRIP);
			olVertex(terrain[i].x - camerax,
				 terrain[i].y - cameray, GROUNDCOLOR);
			first = 0;
		}
		olVertex( terrain[i + 1].x - camerax,
			 terrain[i + 1].y - cameray, GROUNDCOLOR);

		if (terrain[i].x < lander->x &&
			terrain[i + 1].x > lander->x) {
			dx = terrain[i + 1].x - terrain[i].x;
			dy = terrain[i + 1].y - terrain[i].y;
			if (dy == 0) {
				if (lander->y + 30 > terrain[i].y)
					collision();
			} else {
					yintersect = (int) ((float) (lander->x - terrain[i].x) * ((float) dy / (float) dx) + terrain[i].y);
					if (lander->y + 30 > yintersect)
						collision();
			}
		}
	}
	olEnd();
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
	midy += (short) ((((double) rand() / (double) RAND_MAX) - 0.5) * dist * 0.4);
	terrain[midpoint].x = midx;
	terrain[midpoint].y = midy;
	generate_terrain(first, midpoint);
	generate_terrain(midpoint, last);
}

int find_free_obj(void);
static void add_landing_pads(int n)
{
	int i, x, j;
	struct object *k;

	x = NTERRAINPTS / (n + 1);
	for (i = 0; i < n; i++) {
		/* make a level spot */
		terrain[x].y = terrain[x + 1].y;
		j = find_free_obj();
		k = &o[j];
		k->x = (terrain[x + 1].x - terrain[x].x) / 2;
		k->x += terrain[x].x;
		k->y = terrain[x].y - 3;
		k->draw = draw_landing_pad;
		k->move = no_move;
		k->alive = 1;
		x += NTERRAINPTS / (n + 1);
	}
}

static init_terrain(void)
{
	int first = 0;
	int last = NTERRAINPTS - 1;

	terrain[first].x = 0;
	terrain[first].y = 0;
	terrain[last].x = (short) TERRAIN_WIDTH;
	terrain[last].y = 0;
	generate_terrain(first, last);
	add_landing_pads(3);
}

int find_free_obj(void)
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

	int first = 1;
	for (i = 0; i < font[letter]->npoints-1; i++) {
		if (font[letter]->p[i+1].x == LINE_BREAK) {
			i += 2;
			olVertex(x + font[letter]->p[i].x,y + font[letter]->p[i ].y,C_BLACK);
		}
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
		
		if (x1 > 0 && x2 > 0) {
			if ( first ) {
				olBegin(OL_LINESTRIP);
				olVertex(x1,y1,openlase_color);
				first = 0;
			}
			olVertex(x2, y2, openlase_color);
		}
	}
	olEnd();
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

static void rainbow_abs_xy_draw_string(char *s, int font, int x, int y) 
{

	int i;	
	int dx, deltax = 0;
	int color;
	static int rbtimer = 0; 
		
	for (i = 0; s[i] ; i++) {
		color = ((((x + deltax) * NRAINBOWCOLORS) / ((int) SCREEN_WIDTH * 2)
				+ rbtimer) %
				NRAINBOWCOLORS);
		openlase_color = rainbow_color[color];
		dx = (font_scale[font]) +
			abs_xy_draw_letter(gamefont[font], s[i], x + deltax, y);  
		deltax += dx;
	}
	rbtimer += 2;
}

static void draw_objs(void)
{
	int i;

	for (i = 0; i < MAXOBJS; i++)
		if (o[i].alive)
			o[i].draw(&o[i]);
}

static void move_generic(struct object *o, float elapsed_time)
{
	if (o->x + o->vx * elapsed_time < 0)
		o->vx = -o->vx;
	if (o->x + o->vx * elapsed_time > TERRAIN_WIDTH)
		o->vx = -o->vx;
	o->x += o->vx * elapsed_time;
	o->y += o->vy * elapsed_time;
	o->vy += gravity;
}

static void remove_landing_pads(void)
{
	int i;

	for (i = 0; i < MAXOBJS; i++) {
		if (o[i].alive && o[i].draw == draw_landing_pad) {
			o[i].alive = 0;
			o[i].draw = NULL;
			free_object(i);
		}
	}
}

static void move_lander(struct object *o, float elapsed_time)
{
	static int crash_timer = 0;

	if (crash_screen) {
		o->vx = 0;
		o->vy = 0;
		crash_timer--;
		if (crash_timer == 0) {
			if (!successful_landing) {
				remove_landing_pads();
				init_terrain();
				o->x = SCREEN_WIDTH / 2 + 0.1;
				o->y = terrain[NTERRAINPTS / 32].y - 300;
				o->vx = 150;
				o->vy = 0;
				o->v = &lander_vect;
				attract_mode_active = 1;
				wwviaudio_add_sound(GO_FOR_LANDING);
			} else {
				o->vx = 0;
				o->vy = -100;
				exhaust(o->x - o->vx * elapsed_time,
					o->y - o->vy * elapsed_time + 65,
					o->vx, o->vy + 400, 20, SPARKLIFE);
			}
			crash_screen = 0;
			landing_evaluated = 0;
		}
	} else {
		crash_timer = 100;
	}
	move_generic(o, elapsed_time);
	if (o->vy < -130 || o->vy > 60)
		add_sound(CHECK_VVELOCITY);
	if (o->vx < -155 || o->vx > 155)
		add_sound(CHECK_HVELOCITY);
	if (requested_thrust > 0.0 && fuel > 0.0) {
		o->vy -= gravity * 5.0;
		exhaust(o->x - o->vx * elapsed_time, o->y - o->vy * elapsed_time + 65,
			o->vx, o->vy + 400, 2, SPARKLIFE);
		play_thruster_sound();
	}
	if (requested_left > 0.0 && fuel > 0.0) {
		o->vx -= gravity * 3.0;
		exhaust(o->x + 40 - o->vx * elapsed_time, o->y - o->vy * elapsed_time,
			o->vx + 200, o->vy, 2, SPARKLIFE);
		play_thruster_sound();
	}
	if (requested_right > 0.0 && fuel > 0.0) {
		o->vx += gravity * 3.0;
		exhaust(o->x - 40 - o->vx * elapsed_time, o->y - o->vy * elapsed_time,
			o->vx - 200, o->vy, 2, SPARKLIFE);
		play_thruster_sound();
	}
}

static void move_objs(float elapsed_time)
{
	int i;
	int temp_highest_obj = -1;

	for (i = 0; i < MAXOBJS; i++)
		if (o[i].alive) {
			if (i > temp_highest_obj)
				temp_highest_obj = i;
			o[i].move(&o[i], elapsed_time);
		}
	highest_object_number = temp_highest_obj;
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

	openlase_color = 0x1f1fff;
	rainbow_abs_xy_draw_string("LASER LANDER", BIG_FONT, 120, y);
	y += vy;
	if (y > 600)
		vy = -5;
	if (y < 300)
		vy = 5;
	rainbow_abs_xy_draw_string("(c) Stephen M. Cameron 2013", SMALL_FONT, 250, 700); 
}

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
	if (jse.button[3] == 1)
		requested_thrust = 1.0;
	else
		requested_thrust = 0.0;
	if (jse.button[2] == 1)
		requested_left = 1.0;
	else
		requested_left = 0.0;
	if (jse.button[1] == 1)
		requested_right = 1.0;
	else
		requested_right = 0.0;
}

static void setup_vects(void)
{
	setup_vect(lander_vect, lander_points);
	setup_vect(wreckage_vect, wreckage_points);
}

static void move_camera(void)
{
	int dcx, dcy; /* desired camera position */
	float px, py;
	float maxspeed = 300;

	dcx = camerax;
	dcy = cameray;

	px = fabs(lander->vx) / maxspeed;
	if (px > 1.0)
		px = 1.0;
	py = fabs(lander->vy) / maxspeed;
	if (py > 1.0)
		py = 1.0;

	px = 1.0 - px;
	py = 1.0 - py;

	if (lander->vx > 0)
		dcx = lander->x - (1 - px) * 300 - (px * (float) SCREEN_WIDTH / 2.0);
	if (lander->vx < 0)
		dcx = lander->x - (1 - px) * 700 - (px * (float) SCREEN_WIDTH / 2.0);
	if (lander->vy > 0)
		dcy = lander->y - (1 - py) * 300 - (py * (float) SCREEN_HEIGHT / 2.0);
	if (lander->vy < 0)
		dcy = lander->y - (1 - py) * 700 - (py * (float) SCREEN_HEIGHT / 2.0);
	camerax += (int) (0.3 * (dcx - camerax));
	cameray += (int) (0.3 * (dcy - cameray));
}

static void draw_crash_screen(void)
{
	if (!crash_screen)
		return;

	if (successful_landing) {
		openlase_color = 0x1fff1f;
		abs_xy_draw_string(evaluate_landing(), BIG_FONT, 120, 200);
	} else {
		openlase_color = 0xff1f1f;
		abs_xy_draw_string("CRASH!", BIG_FONT, 120, 200);
	}
}

static void setup_spark_colors(void)
{

	/* Set up an array of colors that fade nicely from bright
	 * yellow to orange to red.
	 */

	int i, r, g, b, dr, dg, db;

	r = 32766 * 2;
	g = 32766 * 2;
	b = 32766 * 1.5;

	dr = 0;
	dg = (-(2500) / NSPARKCOLORS);
	db = 3 * dg;

	for (i = NSPARKCOLORS - 1; i >= 0; i--) {
		sparkcolor[i] = ((r >> 8) << 16) |
				((g >> 8) << 8) |
				((b >> 8));
		r += dr;
		g += dg;
		b += db;

		if (r < 0) r = 0;
		if (g < 0) g = 0;
		if (b < 0) b = 0;

		dg *= 1.27;
		db *= 1.27;
	}
}

static void setup_rainbow_colors(void)
{

	int i, r, g, b, dr, dg, db, c;

	r = 32766*2;
	g = 0;
	b = 0;

	dr = -r / NRAINBOWSTEPS;
	dg = r / NRAINBOWSTEPS;
	db = 0;

	c = 0;

	for (i = 0; i < NRAINBOWSTEPS; i++) {
		rainbow_color[c] = ((r >> 8) << 16) |
				((g >> 8) << 8) |
				(b >> 8);

		r += dr;
		g += dg;
		b += db;

		c++;
	}

	dg = (-32766 * 2) / NRAINBOWSTEPS;
	db = -dg;
	dr = 0;

	for (i = 0; i < NRAINBOWSTEPS; i++) {
		rainbow_color[c] = ((r >> 8) << 16) |
					((g >> 8) << 8) |
					(b >> 8);

		r += dr;
		g += dg;
		b += db;

		c++;
	}

	db = (-32766 * 2) / NRAINBOWSTEPS;
	dr = -db;
	dg = 0;

	for (i = 0;i < NRAINBOWSTEPS; i++) {
		rainbow_color[c] = ((r >> 8) << 16) |
					((g >> 8) << 8) |
					(b >> 8); 
		r += dr;
		g += dg;
		b += db;

		c++;
	}
}


int main(int argc, char *argv[])
{
	float elapsed_time = 0.0;
	struct timeval tv;
	int audio_failed = 0;

	audio_failed = wwviaudio_initialize_portaudio(10, NUMSOUNDS);
	if (audio_failed)
		fprintf(stderr, "Failed to initialized portaudio, sound won't work.\n");
	else
		read_audio_data();
	memset(o, 0, sizeof(o));
	gettimeofday(&tv, NULL);
	srand(tv.tv_usec);
	last_sound_time = tv;

	free_obj_bitmap[0] = 0x01;
	lander->x = SCREEN_WIDTH / 2 + 0.1;
	lander->y = 0;
	lander->vx = 150;
	lander->vy = 0;
	lander->alive = 1;
	lander->v = &lander_vect;
	lander->draw = draw_lander;
	lander->move = move_lander;
	lander->n = 0;

	setup_spark_colors();
	setup_rainbow_colors();
	setup_vects();
	init_terrain();
	lander->y = terrain[NTERRAINPTS / 32].y - 300;
	joystick_fd = open_joystick(JOYSTICK_DEVICE, NULL);
	if (joystick_fd < 0)
		printf("No joystick...");

	snis_typefaces_init();

	if (setup_openlase())
		return -1;

	while(1) {
		move_camera();
		deal_with_joystick();
		draw_crash_screen();
		draw_objs();
		attract_mode();
		draw_terrain();
		openlase_renderframe(&elapsed_time);
		move_objs(elapsed_time);
	}
	olShutdown();
	if (joystick_fd >= 0)
		close_joystick();
	if (!audio_failed) {
		wwviaudio_cancel_all_sounds();
		wwviaduio_stop_portaudio();
	}
	return 0;
}

