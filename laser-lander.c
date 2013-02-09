#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "libol.h"

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
	olScale(2,-2);
	/* window is now 0,0 - 1,1 with y increasing down, x increasing right */

	return 0;
}

static void openlase_renderframe(float *elapsed_time)
{
	*elapsed_time = olRenderFrame(60);
	olLoadIdentity();
	olTranslate(-1,1);
	olScale(2,-2);
	/* window is now 0,0 - 1,1 with y increasing down, x increasing right */
}

int main(int argc, char *argv[])
{
	float elapsed_time = 0.0;

	if (setup_openlase())
		return -1;


	while(1) {
		draw_objs();
		openlase_renderframe(&elapsed_time);
		move_objs(elapsed_time);
	}
	olShutdown();
	return 0;
}

