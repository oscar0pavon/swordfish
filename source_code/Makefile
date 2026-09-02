
CLINKS := -lpthread -lm -lvulkan -ldrm -llodepng
CLINKS += -linput -ludev -lxkbcommon
CLINKS += -lwayland-server
#INFO sword itself no longer calls pway - there is no windowed dev path any
#more - but libpengine.a's pe_vk_create_surface() still references
#pway_display/pway_surface in its (dead, since is_drm_rendering is always
#true here) non-DRM branch, and those symbols live in libpway. dropping this
#link fails at link time even though nothing sword does ever reaches that code
CLINKS += -lpway -lwayland-client -lwayland-egl
#INFO no -lseat. seatd hands DRM and input fds to a compositor that is not
#root and arbitrates between several users' sessions; sword is root on a
#single user machine and opens /dev/dri/card0 itself. the half of libseat that
#is not about multiple users - being told when the VT went away - is the
#kernel's own VT_PROCESS protocol, and lives in tty.c
CLINKS += -lEGL -lGL
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

CFLAGS_ENGINE := $(CFLAGS) -I/root/sword/source_code $(PENGINE_INCLUDES) -I/usr/include/libdrm -I/usr/include -I/usr/local/include

#a .d per object, fed back by -include below, so touching a header in pengine
#or pway rebuilds what reads it instead of leaving a stale struct layout
CFLAGS_ENGINE += -MMD -MP

#everything that used to be compositor/*.c is flattened in here now
SRCS := $(wildcard *.c)
OBJS := $(SRCS:c=o)
DEPS := $(OBJS:.o=.d)

#the client quads (surface.c) and the cursor are all that is drawn now - the
#city, hud and model shaders went to 3dtop with the scene
SPVS := ../shaders/texture_frag.spv
SPVS += ../shaders/dimention_2d_vert.spv
SPVS += ../shaders/cursor_frag.spv

#-rdynamic puts sword's own symbols in the dynamic symbol table, which is
#what backtrace_symbols_fd() reads. without it the crash backtrace log.c writes
#is a column of addresses with no names on it
#named for where it lands, or it matches no file and relinks every run
sword: ../sword

../sword: $(OBJS) $(SPVS)
	cc $(CFLAGS_ENGINE) -rdynamic -L/usr/local/lib $(OBJS) -o $@ $(CLINKS)

%.o: %.c
	cc $(CFLAGS_ENGINE) -c $< -o $@

shaders: $(SPVS)

#../shaders is gitignored, so a fresh clone has nowhere to write. order-only,
#or the directory's mtime would rebuild every shader
../shaders/%_frag.spv: shaders/%.frag | ../shaders
	glslc $< -o $@

../shaders/%_vert.spv: shaders/%.vert | ../shaders
	glslc $< -o $@

../shaders:
	mkdir -p $@

clean:
	rm -f ../sword
	rm -f *.o
	rm -f *.d
	rm -f ../shaders/*.spv

-include $(DEPS)

.PHONY: sword shaders clean
