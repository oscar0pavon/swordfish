

all: sword
	make install
	

sword:
	make -C source_code


clean:
	make -C source_code clean


compile_commands:
	make --always-make --dry-run -C ./source_code


install:#TODO create directories in /usr/libexec/
	cp sword /usr/bin
	cp -r shaders /usr/libexec/sword
	cp images/* /usr/libexec/sword/images

.PHONY: sword
