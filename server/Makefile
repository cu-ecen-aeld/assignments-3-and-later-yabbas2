CROSS_COMPILE ?=
CC=$(CROSS_COMPILE)gcc

all default: aesdsocket.c
	$(CC) $^ -o aesdsocket

clean:
	rm -f aesdsocket
