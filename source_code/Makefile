
CLINKS := -lpthread -lm -lvulkan -ldrm -llodepng 
CLINKS += -linput -ludev -lxkbcommon
CLINKS += -lwayland-server
CLINKS += -lpway -lwayland-client -lwayland-egl
CLINKS += -lseat -lEGL -lGL
#INFO the engine and the vulkan renderer live in /root/pengine now and arrive
#as one static archive. it comes before the libraries it needs, not after
CLINKS := -lpengine $(CLINKS)

CFLAGS := -g

#pengine's headers are full of tentative definitions - `bool thread_main;` in
#threads.h and about a hundred more - so every object that includes one gets
#its own. they merge under -fcommon and collide at link time without it;
#pengine builds with both of these itself
CFLAGS += -fcommon

#pengine's headers are installed under the same tree they are written against,
#so <engine/...> resolves against this one, and the "renderer/..." includes
#inside them resolve relative to the header doing the including
PENGINE_INCLUDES := -I/usr/local/include/pengine

#main_camera is a CameraComponent, one trailing field longer than the Camera
#that camera_init() and pe_camera_look_at() take
CFLAGS += -Wno-incompatible-pointer-types

CFLAGS_ENGINE := $(CFLAGS) -I/root/swordfish/source_code $(PENGINE_INCLUDES) -I/usr/include/libdrm -I/usr/include -I/usr/local/include

compositor_src := $(wildcard ./compositor/*.c)
compositor_objs := $(compositor_src:%.c=%.o)

SRCS := $(wildcard *.c)
SWORDFISHOBJS := $(SRCS:c=o)

OBJS := $(SWORDFISHOBJS) $(compositor_objs)

swordfish: $(OBJS) shaders
	cc $(CFLAGS_ENGINE) -L/usr/local/lib $(OBJS) -o ../swordfish $(CLINKS)

%.o: %.c
	cc $(CFLAGS_ENGINE) -c $<

./compositor/%.o: ./compositor/%.c
	cc $(CFLAGS_ENGINE) -o $@ -c $<

#the client quads (compositor/surface.c) and the cursor are all that is drawn
#now - the city, hud and model shaders went to 3dtop with the scene
shaders: shaders/texture.frag shaders/dimention_2d.vert
	glslc shaders/texture.frag -o ../shaders/texture_frag.spv
	glslc shaders/dimention_2d.vert -o ../shaders/dimention_2d_vert.spv
	glslc shaders/cursor.frag -o ../shaders/cursor_frag.spv

clean:
	rm -f ../swordfish
	rm -f *.o
	rm -f ./compositor/*.o
	rm -f ../shaders/*.spv

.PHONY: shaders
