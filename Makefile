CC=gcc
CFLAGS=-Wall -g

BINS= libmyalloc.so

all: $(BINS)

libmyalloc.so:	allocator.c
	$(CC) -O2 -DNDEBUG -Wall -fPIC -shared allocator.c -o libmyalloc.so -lm

clean:
	rm $(BINS)
