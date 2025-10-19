
CLINKS := -lX11 -lpthread -lm -lvulkan

CFLAGS := -g

CFLAGS_ENGINE := $(CFLAGS) -I/root/swordfish/source_code

vulkan_src := $(wildcard ./renderer/*.c)
vulkan_objs := $(vulkan_src:%.c=%.o)

engine_src := $(wildcard ./engine/*.c)
engine_objs := $(engine_src:%.c=%.o)

OBJS := input.o window.o $(vulkan_objs) $(engine_objs)


swordfish: main.c $(OBJS) shaders
	cc $(CFLAGS_ENGINE) main.c $(OBJS) -o ../swordfish $(CLINKS)

window.o: window.c
	cc $(CFLAGS) -c $<

input.o: input.c
	cc $(CFLAGS) -c $<

./engine/%.o: ./engine/%.c
	cc $(CFLAGS_ENGINE) -o $@ -c $<

./renderer/%.o: ./renderer/%.c
	cc $(CFLAGS_ENGINE) -o $@ -c $<

shaders: shaders/red.frag shaders/model_view_projection.vert
	glslc shaders/red.frag -o ../shaders/red_frag.spv
	glslc shaders/model_view_projection.vert -o ../shaders/model_view_projection_vert.spv

clean:
	rm -f ../swordfish
	rm -f *.o
	rm -f ./engine/*.o
	rm -f ./renderer/*.o
	rm -f ../shaders/*.spv

.PHONY: shaders

