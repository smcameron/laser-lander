 
laser-lander:	laser-lander.c
	$(CC) -g -L. -o laser-lander laser-lander.c -lopenlase

clean:
	rm -fr laser-lander


