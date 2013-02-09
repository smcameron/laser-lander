#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <math.h>

#include "libol.h"

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

static void openlase_renderframe(void)
{
	float ftime;

	ftime = olRenderFrame(60);
	olLoadIdentity();
	olTranslate(-1,1);
	olScale(2,-2);
	/* window is now 0,0 - 1,1 with y increasing down, x increasing right */
}

int main(int argc, char *argv[])
{
	if (setup_openlase())
		return -1;

	while(1) {
		olLine(0.0, 0.0, 1.0, 1.0, C_WHITE);
		openlase_renderframe();
		usleep(1000);
	}
	olShutdown();
	return 0;
}

