# Makefile — ccheck (Dueño: Dev 1)
#
#   make                 compila ccheck con todas las fases reales (STUBS=0)
#   make STUBS=1         compila usando stubs.c en lugar de Dev 3/Dev 4
#   make run FILE=x.c    ejecuta ccheck sobre x.c (salidas en salidas/)
#   make test            corre todos los casos de pruebas/ (salidas/<caso>/)
#   make test-lexer      compara tokens.txt y errores.txt de los casos de Dev 1
#   make dot             convierte salidas/arbol.dot a salidas/arbol.png
#   make clean           borra archivos generados

CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -g -D_POSIX_C_SOURCE=200809L
ifneq ($(wildcard /opt/homebrew/opt/bison/bin/bison),)
  BISON ?= /opt/homebrew/opt/bison/bin/bison
else ifneq ($(wildcard /usr/local/opt/bison/bin/bison),)
  BISON ?= /usr/local/opt/bison/bin/bison
else
  BISON ?= bison
endif
FLEX    = flex
BIN     = ccheck
STUBS  ?= 0
FILE   ?= entrada.c
OUT    ?= salidas

SRC = main.c diag.c token.c
ifeq ($(STUBS),1)
  SRC += stubs.c
else
  SRC += symtab.c semantico.c interp.c arbol.c
endif

OBJ  = $(SRC:.c=.o) parser.tab.o lex.yy.o
HDRS = token.h diag.h ast.h fases.h types.h symtab.h parser.tab.h

.PHONY: all run test test-lexer dot clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^

parser.tab.c: parser.y
	$(BISON) -d -Wall parser.y

parser.tab.h: parser.tab.c

lex.yy.c: lexer.l
	$(FLEX) -o lex.yy.c lexer.l

# El código generado por Flex/Bison produce algunos avisos inofensivos.
lex.yy.o: lex.yy.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-unused-function -Wno-sign-compare -c $< -o $@

parser.tab.o: parser.tab.c $(HDRS)
	$(CC) $(CFLAGS) -Wno-unused-function -c $< -o $@

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN) -o $(OUT) $(FILE)

test: $(BIN)
	@for f in pruebas/*.c pruebas/dev1/*.c; do \
	  [ -e "$$f" ] || continue; \
	  caso=$$(basename $$f .c); \
	  ./$(BIN) -o $(OUT)/$$caso $$f > $(OUT)/$$caso.log 2>&1; \
	  printf '%-32s código de salida %s\n' "$$f" "$$?"; \
	done

test-lexer: $(BIN)
	@ok=0; fallos=0; \
	for esp in pruebas/dev1/esperado/*.tokens.txt; do \
	  caso=$$(basename $$esp .tokens.txt); \
	  ./$(BIN) -o $(OUT)/$$caso pruebas/dev1/$$caso.c > /dev/null 2>&1; \
	  if diff -u $$esp $(OUT)/$$caso/tokens.txt && \
	     diff -u pruebas/dev1/esperado/$$caso.errores.txt $(OUT)/$$caso/errores.txt; \
	  then ok=$$((ok+1)); \
	  else fallos=$$((fallos+1)); echo "FALLA: $$caso"; fi; \
	done; \
	echo "test-lexer: $$ok correctos, $$fallos fallidos"; [ $$fallos -eq 0 ]

dot:
	dot -Tpng $(OUT)/arbol.dot -o $(OUT)/arbol.png

clean:
	rm -f $(BIN) *.o lex.yy.c parser.tab.c parser.tab.h parser.output
