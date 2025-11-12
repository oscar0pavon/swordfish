
CLINKS := -lX11 -lpthread -lm -lvulkan -ldrm -lgbm -llodepng 
CLINKS += -linput -ludev -lxkbcommon
CLINKS += -lwayland-server

CFLAGS := -g

CFLAGS_ENGINE := $(CFLAGS) -I/root/swordfish/source_code -I/usr/include/libdrm -I/usr/include
CFLAGS_ENGINE += -DCGLM_FORCE_DEPTH_ZERO_TO_ONE -DCGLM_FORCE_LEFT_HANDED

compositor_src := $(wildcard ./compositor/*.c)
compositor_objs := $(compositor_src:%.c=%.o)

vulkan_src := $(wildcard ./renderer/*.c)
vulkan_objs := $(vulkan_src:%.c=%.o)

engine_src := $(wildcard ./engine/*.c)
engine_objs := $(engine_src:%.c=%.o)


SRCS := $(wildcard *.c)
SWORDFISHOBJS := $(SRCS:c=o)

OBJS := $(SWORDFISHOBJS) $(vulkan_objs) $(engine_objs) $(compositor_objs)

swordfish: $(OBJS) shaders
	cc $(CFLAGS_ENGINE) $(OBJS) -o ../swordfish $(CLINKS)

%.o: %.c
	cc $(CFLAGS_ENGINE) -c $<

./compositor/%.o: ./compositor/%.c
	cc $(CFLAGS_ENGINE) -o $@ -c $<

./engine/%.o: ./engine/%.c
	cc $(CFLAGS_ENGINE) -o $@ -c $<

./renderer/%.o: ./renderer/%.c
	cc $(CFLAGS_ENGINE) -o $@ -c $<

shaders: shaders/red.frag shaders/model_view_projection.vert
	glslc shaders/red.frag -o ../shaders/red_frag.spv
	glslc shaders/texture.frag -o ../shaders/texture_frag.spv
	glslc shaders/model_view_projection.vert -o ../shaders/model_view_projection_vert.spv
	glslc shaders/dimention_2d.vert -o ../shaders/dimention_2d_vert.spv

clean:
	rm -f ../swordfish
	rm -f *.o
	rm -f ./engine/*.o
	rm -f ./renderer/*.o
	rm -f ../shaders/*.spv

.PHONY: shaders

