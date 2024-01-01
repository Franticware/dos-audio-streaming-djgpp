CFLAGS	= -g -Wall 
L	= -lpc -lg -lm 
CC = i586-pc-msdosdjgpp-gcc

.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS	= \
	main.o sb.o

prog : $(OBJS)
	$(CC) -o sbstream.exe $(OBJS) $(L) 

clean:
	rm *.o sbstream.exe
