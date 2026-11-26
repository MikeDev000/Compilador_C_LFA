#ifndef SYMTAB_H
#define SYMTAB_H
/*
 * symtab.h — Tabla de símbolos basada en pila de ámbitos (Dueño: Dev 3).
 */
#include <stdbool.h>
#include "types.h"

typedef struct Simbolo {
  char *nombre;
  TipoDato tipo;
  int linea;
  int col;
  int nivel_ambito;
  struct Simbolo *siguiente;
} Simbolo;

typedef struct Ambito {
  int nivel;
  Simbolo *simbolos;
  struct Ambito *padre;
} Ambito;

typedef struct TablaSimbolos {
  Ambito *tope;
  int nivel_actual;
} TablaSimbolos;

void symtab_init(TablaSimbolos *ts);
void symtab_free(TablaSimbolos *ts);
void symtab_enter_scope(TablaSimbolos *ts);
void symtab_exit_scope(TablaSimbolos *ts);

Simbolo *symtab_lookup_current(const TablaSimbolos *ts, const char *nombre);
Simbolo *symtab_lookup(const TablaSimbolos *ts, const char *nombre);
bool symtab_insert(TablaSimbolos *ts, const char *nombre, TipoDato tipo,
                   int linea, int col, Simbolo **existente_out);

#endif /* SYMTAB_H */
