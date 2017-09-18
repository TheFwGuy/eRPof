CC=msp430-gcc
#CFLAGS=-O0 -Wall -g -mmcu=msp430f2012
CFLAGS=-O0 -Wall -g -mmcu=msp430f2002

OBJS=RPOf.o


all: $(OBJS)
	$(CC) $(CFLAGS) -o RPOf.elf $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $<

clean:
	rm -fr RPOf.elf $(OBJS)
