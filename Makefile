

all:
	make -C source_code


clean:
	make -C source_code clean


compile_commands:
	make --always-make --dry-run -C ./source_code


install:
	cp swordfish /usr/bin
	cp -r shaders /usr/libexec/swordfish
	cp -r models /usr/libexec/swordfish
