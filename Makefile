
# To compile withaudio, WITHAUDIO=yes, 
# for no audio support, change to WITHAUDIO=no, 
WITHAUDIO=yes
# WITHAUDIO=no

ifeq (${WITHAUDIO},yes)
SNDLIBS=`pkg-config --libs portaudio-2.0 vorbisfile`
SNDFLAGS=-DWITHAUDIOSUPPORT `pkg-config --cflags portaudio-2.0` -DDATADIR=\"./sounds\"
OGGOBJ=ogg_to_pcm.o
else
SNDLIBS=
SNDFLAGS=-DWWVIAUDIO_STUBS_ONLY -DDATADIR=\"./sounds\"
OGGOBJ=
endif

DEFINES=${SNDFLAGS}

all:	laser-lander

ogg_to_pcm.o:	ogg_to_pcm.c ogg_to_pcm.h Makefile
	$(CC) ${DEBUG} ${PROFILE_FLAG} ${OPTIMIZE_FLAG} `pkg-config --cflags vorbisfile` \
		-pthread ${WARNFLAG} -c ogg_to_pcm.c

wwviaudio.o:	wwviaudio.c wwviaudio.h ogg_to_pcm.h my_point.h Makefile
	$(CC) ${WARNFLAG} ${DEBUG} ${PROFILE_FLAG} ${OPTIMIZE_FLAG} \
		${DEFINES} \
		-pthread `pkg-config --cflags vorbisfile` \
		-c wwviaudio.c

joystick.o:	joystick.c joystick.h compat.h
	$(CC) -g -c joystick.c

snis_font.o:	my_point.h snis_font.c snis_font.h
	$(CC) -g -c snis_font.c

snis_typeface.o:	snis_typeface.h snis_typeface.c snis_font.h
	$(CC) -g -c snis_typeface.c
 
laser-lander:	laser-lander.c snis_font.o snis_typeface.o joystick.o wwviaudio.o ogg_to_pcm.o
	$(CC) -g -L. -o laser-lander laser-lander.c -lopenlase snis_font.o snis_typeface.o joystick.o wwviaudio.o ogg_to_pcm.o ${DEFINES} -pthread `pkg-config --cflags vorbisfile` ${SNDLIBS}

clean:
	rm -fr laser-lander *.o


