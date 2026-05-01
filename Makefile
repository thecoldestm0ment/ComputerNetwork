myserver : server.o
	gcc -o myserver server.o

server.o : server.c
	gcc -c server.c

clean :
	rm -f myserver server.o