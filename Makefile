CC = gcc
CFLAGS = -g -w

OUT_DIR = bin
.PHONY = all

all: directories ast compiler pre

pre: parse.c lex.c pre.c
	@echo "Building preprocessor"
	@$(CC) -m64 $(CFLAGS) -DSTANDALONE parse.c lex.c pre.c -o bin/pre64

compiler: main.c lex.c ast.c x86.c pe.c elf.c pre.c parse.c memory.c
	@echo "Building compiler"
	@$(CC) -m64 $(CFLAGS) main.c lex.c ast.c x86.c pe.c elf.c pre.c parse.c memory.c -o bin/ocean64

ast: main-ast.c lex.c ast.c pre.c parse.c
	@echo "Building AST"
	@$(CC) -m64 $(CFLAGS) main-ast.c lex.c ast.c pre.c parse.c -o bin/ast64

directories: ${OUT_DIR}

${OUT_DIR}:
	@mkdir -p bin

clean:
	@echo "Cleaning up"
	rm bin/*
