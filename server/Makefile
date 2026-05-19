CC=$(CROSS_COMPILE)gcc
EXTRA_FLAGS= -Wall -Werror
LDLIBS= -lpthread

default: aesdsocket
all: aesdsocket

aesdsocket: aesdsocket.o
	$(CC) -o aesdsocket aesdsocket.o $(LDLIBS)

aesdsocket.o: aesdsocket.c
	$(CC) $(EXTRA_FLAGS) -c aesdsocket.c

clean:
	rm -f aesdsocket *.o
