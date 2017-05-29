CC = gcc
SRC = xcc.c lex_parse.c lex_emit_code.c lex_vm.c
OBJ = $(SRC:%.c=%.o)

.PHONY: all
all: a.out

a.out: $(OBJ)
	$(CC) -Wall -O2 -o $@ $(OBJ)

.c.o:
	$(CC) -Wall -c -std=c99 $<

.PHONY: clean
clean: 
	rm -f *.out *.o *~ 
