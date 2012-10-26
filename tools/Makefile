CC = g++
CFLAGS ?= -Wall -O2 -g

all: serviceref

serviceref: serviceref.o
	$(CC) serviceref.o -o serviceref

clean:
	rm -f *.o
	rm -f serviceref
