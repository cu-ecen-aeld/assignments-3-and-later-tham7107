######################################################################
##
## Thomas Ames
## ECEA 5305, assignment #5, Makefile for aesdsocket.c
## July 2023
##

CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
LDFLAGS ?= -pthread
CFLAGS += -I../aesd-char-driver

all: aesdsocket

clean:
	rm -f aesdsocket *.o

aesdsocket: aesdsocket.o
	$(CC) -o $@ $^ $(LDFLAGS)
