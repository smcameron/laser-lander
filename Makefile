
all:	laser-lander

joystick.o:	joystick.c joystick.h compat.h
	$(CC) -g -c joystick.c

snis_font.o:	my_point.h snis_font.c snis_font.h
	$(CC) -g -c snis_font.c

snis_typeface.o:	snis_typeface.h snis_typeface.c snis_font.h
	$(CC) -g -c snis_typeface.c
 
laser-lander:	laser-lander.c snis_font.o snis_typeface.o joystick.o
	$(CC) -g -L. -o laser-lander laser-lander.c -lopenlase snis_font.o snis_typeface.o joystick.o

clean:
	rm -fr laser-lander *.o


