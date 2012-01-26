CC ?= gcc
CFLAGS ?= -O2 -g -Wall
CFLAGS += -std=c99

.PHONY: all clean
.SUFFIXES:

all: tchroot

clean:
	-rm -f tchroot tchroot.o

%: %.o
	$(CC) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

ld-linux.so.2: ld-i386.S
	tchroot i386 -- $(CC) -o $@ $< -nostdlib -s
