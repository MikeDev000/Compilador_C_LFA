/*
 * interp.c — Intérprete simbólico estático de E/S (Dueño: Dev 3).
 *
 * Emite el reporte estático de llamadas a printf() y scanf() sobre un árbol
 * validado semánticamente, sin evaluar valores ni ejecutar el programa.
 */
#include "ast.h"
#include "symtab.h"
#include "token.h"
#include "types.h"

#include <stdio.h>
#include <string.h>

static bool es_nodo(const Nodo *n, const char *nombre) {
  return n && n->nombre && strcmp(n->nombre, nombre) == 0;
}

static const char *lexema_de(const Nodo *n) {
  if (!n) return "";
  if (n->token && n->token->lexema) return n->token->lexema;
  return n->nombre ? n->nombre : "";
}

static void quitar_comillas(const char *src, char *dst, size_t cap) {
  if (!src || cap == 0) {
    if (cap) dst[0] = '\0';
    return;
  }
  size_t len = strlen(src);
  if (len >= 2 && src[0] == '"' && src[len - 1] == '"') {
    size_t inner = len - 2;
    if (inner >= cap) inner = cap - 1;
    memcpy(dst, src + 1, inner);
    dst[inner] = '\0';
  } else {
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
  }
}

static void expr_a_texto(const Nodo *n, char *buf, size_t cap) {
  if (!n || cap == 0) return;
  if (n->nhijos == 0) {
    strncat(buf, lexema_de(n), cap - strlen(buf) - 1);
    return;
  }
  for (int i = 0; i < n->nhijos; i++) {
    const char *lex = lexema_de(n->hijos[i]);
    size_t len = strlen(buf);
    if (i > 0 && len > 0 && buf[len - 1] != '(' &&
        strcmp(lex, ")") != 0 && strcmp(lex, ",") != 0) {
      strncat(buf, " ", cap - len - 1);
    }
    expr_a_texto(n->hijos[i], buf, cap);
  }
}

static void aplanar_args(const Nodo *n, const Nodo **out, int *cnt, int max) {
  if (!n || *cnt >= max) return;
  if (es_nodo(n, "args_printf") || es_nodo(n, "args_scanf")) {
    for (int i = 0; i < n->nhijos; i++) {
      const Nodo *h = n->hijos[i];
      if (h->token && h->token->tipo == DELIM_COMMA) continue;
      aplanar_args(h, out, cnt, max);
    }
    return;
  }
  out[(*cnt)++] = n;
}

static void emitir_printf(const Nodo *n, FILE *out) {
  if (!out || n->nhijos < 6) return;
  const Nodo *args[64];
  int nargs = 0;
  aplanar_args(n->hijos[3], args, &nargs, 64);

  char fmt[1024];
  quitar_comillas(lexema_de(n->hijos[2]), fmt, sizeof(fmt));

  char salida[2048] = "";
  int arg_i = 0;

  for (size_t i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] != '%') {
      size_t len = strlen(salida);
      if (len + 2 < sizeof(salida)) {
        salida[len] = fmt[i];
        salida[len + 1] = '\0';
      }
      continue;
    }
    i++;
    if (fmt[i] == '\0') break;
    if (fmt[i] == '%') {
      strncat(salida, "%", sizeof(salida) - strlen(salida) - 1);
      continue;
    }
    const char *tipo_txt = "desconocido";
    switch (fmt[i]) {
      case 'd': tipo_txt = "int";    break;
      case 'f': tipo_txt = "float";  break;
      case 'c': tipo_txt = "char";   break;
      case 's': tipo_txt = "string"; break;
      default:  break;
    }
    if (arg_i < nargs) {
      char expr_txt[256] = "";
      expr_a_texto(args[arg_i], expr_txt, sizeof(expr_txt));
      if (args[arg_i]->tipo != TIPO_VOID && args[arg_i]->tipo != TIPO_ERROR) {
        tipo_txt = tipo_a_cadena((TipoDato)args[arg_i]->tipo);
      }
      arg_i++;
      char tag[320];
      snprintf(tag, sizeof(tag), "<%s:%s>", tipo_txt, expr_txt);
      strncat(salida, tag, sizeof(salida) - strlen(salida) - 1);
    }
  }
  fprintf(out, "[printf] → \"%s\"\n", salida);
}

static void emitir_scanf(const Nodo *n, FILE *out) {
  if (!out || n->nhijos < 6) return;
  const Nodo *args[64];
  int nargs = 0;
  aplanar_args(n->hijos[3], args, &nargs, 64);

  char fmt[1024];
  quitar_comillas(lexema_de(n->hijos[2]), fmt, sizeof(fmt));

  char linea[2048] = "";
  int arg_i = 0;
  int emitidos = 0;

  for (size_t i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] != '%') continue;
    i++;
    if (fmt[i] == '\0') break;
    if (fmt[i] == '%') continue;

    const char *tipo_txt = (fmt[i] == 'd') ? "int"
                         : (fmt[i] == 'f') ? "float"
                         : (fmt[i] == 'c') ? "char" : "desconocido";

    if (arg_i < nargs && es_nodo(args[arg_i], "arg_dir") && args[arg_i]->nhijos == 2) {
      const char *var = lexema_de(args[arg_i]->hijos[1]);
      char item[160];
      snprintf(item, sizeof(item), "%s<%s> en %s",
               emitidos > 0 ? ", " : "", tipo_txt, var);
      strncat(linea, item, sizeof(linea) - strlen(linea) - 1);
      emitidos++;
    }
    arg_i++;
  }
  if (emitidos > 0) {
    fprintf(out, "[scanf] ← lee %s\n", linea);
  }
}

void interpretar_simbolico(const Nodo *raiz, FILE *out) {
  if (!raiz || !out) return;

  if (es_nodo(raiz, "sent_printf")) {
    emitir_printf(raiz, out);
    return;
  }
  if (es_nodo(raiz, "sent_scanf")) {
    emitir_scanf(raiz, out);
    return;
  }

  for (int i = 0; i < raiz->nhijos; i++) {
    interpretar_simbolico(raiz->hijos[i], out);
  }
}
