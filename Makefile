
PREFIX := /usr
LIBEXEC := $(PREFIX)/libexec/sword

all: sword
	$(MAKE) install


sword:
	$(MAKE) -C source_code


clean:
	$(MAKE) -C source_code clean


compile_commands:
	$(MAKE) --always-make --dry-run -C ./source_code


#surface.c and cursor.c spell $(LIBEXEC)/shaders, and copying the directory
#itself created $(LIBEXEC) as a copy of shaders the first time it ran
install:
	install -d $(LIBEXEC)/shaders $(LIBEXEC)/images $(PREFIX)/bin
	install -m 755 sword $(PREFIX)/bin
	install -m 644 shaders/*.spv $(LIBEXEC)/shaders
	install -m 644 images/* $(LIBEXEC)/images

.PHONY: all sword clean compile_commands install
