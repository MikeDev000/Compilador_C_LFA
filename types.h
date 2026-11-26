#ifndef TYPES_H
#define TYPES_H
/*
 * types.h — Dominio de tipos primitivos y reglas de compatibilidad (Dueño: Dev 3).
 */
#include <stdbool.h>

typedef enum {
  TIPO_VOID = 0,  /* Sentencias / nodos sin tipo de expresión */
  TIPO_INT,
  TIPO_FLOAT,
  TIPO_CHAR,
  TIPO_BOOL,
  TIPO_STRING,    /* Exclusivo para literales de cadena en printf() */
  TIPO_ERROR      /* Centinela para evitar errores en cascada */
} TipoDato;

const char *tipo_a_cadena(TipoDato tipo);
TipoDato tipo_aritmetico_binario(const char *op, TipoDato izq, TipoDato der);
TipoDato tipo_relacional(const char *op, TipoDato izq, TipoDato der);
TipoDato tipo_logico_binario(TipoDato izq, TipoDato der);
bool asignacion_compatible(const char *op_asig, TipoDato destino, TipoDato valor);

#endif /* TYPES_H */
