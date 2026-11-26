/*
 * symtab.c — Implementación de la tabla de símbolos por pila de ámbitos (Dueño: Dev 3).
 */
#include "symtab.h"

#include <stdlib.h>
#include <string.h>

static char *dup_str(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char *c = (char *)malloc(n);
  if (c) memcpy(c, s, n);
  return c;
}

void symtab_init(TablaSimbolos *ts) {
  if (!ts) return;
  ts->tope = NULL;
  ts->nivel_actual = -1;
}

void symtab_enter_scope(TablaSimbolos *ts) {
  if (!ts) return;
  Ambito *nuevo = (Ambito *)malloc(sizeof(Ambito));
  if (!nuevo) return;
  ts->nivel_actual++;
  nuevo->nivel = ts->nivel_actual;
  nuevo->simbolos = NULL;
  nuevo->padre = ts->tope;
  ts->tope = nuevo;
}

void symtab_exit_scope(TablaSimbolos *ts) {
  if (!ts || !ts->tope) return;
  Ambito *viejo = ts->tope;
  ts->tope = viejo->padre;
  ts->nivel_actual--;

  Simbolo *s = viejo->simbolos;
  while (s) {
    Simbolo *sig = s->siguiente;
    free(s->nombre);
    free(s);
    s = sig;
  }
  free(viejo);
}

void symtab_free(TablaSimbolos *ts) {
  if (!ts) return;
  while (ts->tope) symtab_exit_scope(ts);
}

Simbolo *symtab_lookup_current(const TablaSimbolos *ts, const char *nombre) {
  if (!ts || !ts->tope || !nombre) return NULL;
  for (Simbolo *s = ts->tope->simbolos; s != NULL; s = s->siguiente) {
    if (strcmp(s->nombre, nombre) == 0) return s;
  }
  return NULL;
}

Simbolo *symtab_lookup(const TablaSimbolos *ts, const char *nombre) {
  if (!ts || !nombre) return NULL;
  for (Ambito *a = ts->tope; a != NULL; a = a->padre) {
    for (Simbolo *s = a->simbolos; s != NULL; s = s->siguiente) {
      if (strcmp(s->nombre, nombre) == 0) return s;
    }
  }
  return NULL;
}

bool symtab_insert(TablaSimbolos *ts, const char *nombre, TipoDato tipo,
                   int linea, int col, Simbolo **existente_out) {
  if (!ts || !ts->tope || !nombre) return false;

  Simbolo *prev = symtab_lookup_current(ts, nombre);
  if (prev) {
    if (existente_out) *existente_out = prev;
    return false;
  }

  Simbolo *nuevo = (Simbolo *)malloc(sizeof(Simbolo));
  if (!nuevo) return false;
  nuevo->nombre = dup_str(nombre);
  nuevo->tipo = tipo;
  nuevo->linea = linea;
  nuevo->col = col;
  nuevo->nivel_ambito = ts->tope->nivel;
  nuevo->siguiente = ts->tope->simbolos;
  ts->tope->simbolos = nuevo;

  if (existente_out) *existente_out = NULL;
  return true;
}

const char *tipo_a_cadena(TipoDato tipo) {
  switch (tipo) {
    case TIPO_INT:    return "int";
    case TIPO_FLOAT:  return "float";
    case TIPO_CHAR:   return "char";
    case TIPO_BOOL:   return "bool";
    case TIPO_STRING: return "string";
    case TIPO_VOID:   return "void";
    case TIPO_ERROR:  return "<error>";
    default:          return "<desconocido>";
  }
}
