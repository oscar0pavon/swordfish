
CLINKS := -lX11 -lpthread -lm -lvulkan


CFLAGS := -g


OBJS := input.o window.o


swordfish: main.c $(OBJS)
	cc $(CFLAGS) main.c $(OBJS) -o ../swordfish $(CLINKS)

window.o: window.c
	cc $(CFLAGS) -c $<

input.o: input.c
	cc $(CFLAGS) -c $<


clean:
	rm -f ../swordfish
	rm -f *.o
