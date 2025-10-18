
CLINKS := -lX11 -lpthread -lm -lvulkan

CFLAGS := -g

CFLAGS_ENGINE := $(CFLAGS) -I/root/swordfish/source_code

vulkan_src := $(wildcard ./renderer/*.c)
vulkan_objs := $(vulkan_src:%.c=%.o)

engine_src := $(wildcard ./engine/*.c)
engine_objs := $(engine_src:%.c=%.o)

OBJS := input.o window.o $(vulkan_objs) $(engine_objs)


swordfish: main.c $(OBJS)
	cc $(CFLAGS) main.c $(OBJS) -o ../swordfish $(CLINKS)

window.o: window.c
	cc $(CFLAGS) -c $<

input.o: input.c
	cc $(CFLAGS) -c $<

./engine/%.o: ./engine/%.c
	cc $(CFLAGS_ENGINE) -c $<

./renderer/%.o: ./renderer/%.c
	cc $(CFLAGS_ENGINE) -c $<

clean:
	rm -f ../swordfish
	rm -f *.o

