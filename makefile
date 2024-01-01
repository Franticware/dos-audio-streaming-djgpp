CFLAGS	= -g -Wall 
L	= -lpc -lg -lm 
CC = gcc

.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS	= \
	main.o wave.o sb.o

prog : $(OBJS)
	$(CC) -o sound $(OBJS) $(L) 

