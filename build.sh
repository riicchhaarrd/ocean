#!/bin/bash
gcc -g -w main-ast.c lex.c ast.c pre.c parse.c compile.c x86.c && ./a.out examples/q.c
