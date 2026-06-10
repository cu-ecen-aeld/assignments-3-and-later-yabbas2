CROSS_COMPILE ?=

CC?=$(CROSS_COMPILE)gcc
CFLAGS?= -g -Wall -Werror -I$(CURDIR)/../aesd-char-driver/
TARGET?= aesdsocket
LDFLAGS?= -lpthread -lrt

all default: aesdsocket.c
	$(CC) $^ -o $(TARGET) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET)
