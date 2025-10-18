

all:
	make -C source_code


clean:
	make -C source_code clean


compile_commands:
	make --always-make --dry-run -C ./source_code

