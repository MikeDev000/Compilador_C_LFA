/*
 * semantico.c — Fase 3: análisis semántico y comprobación de tipos (Dueño: Dev 3).
 *
 * Implementa el contrato de fases.h:
 *   int analizar_semantica(Nodo *raiz, FILE *out);
 * Reporta todos los errores con reportar_error(SEM, ...) de diag.h y, si no
 * hay errores semánticos, delega el reporte simbólico estático a interp.c.
 */
#include "ast.h"
#include "diag.h"
#include "fases.h"
#include "symtab.h"
#include "token.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Definida en interp.c */
void interpretar_simbolico(const Nodo *raiz, FILE *out);

typedef struct {
  TablaSimbolos ts;
  int errores;
} SemCtx;

static void sem_err(SemCtx *ctx, int linea, int col, const char *msg) {
  ctx->errores++;
  reportar_error(SEM, linea, col, "%s", msg);
}

/* ============================================================================
 * Helpers sobre Nodo (Contrato de nodos de parser.y / Dev 2)
 * ============================================================================ */
static bool es_nodo(const Nodo *n, const char *nombre) {
  return n && n->nombre && strcmp(n->nombre, nombre) == 0;
}

static bool empieza_con(const Nodo *n, const char *prefijo) {
  return n && n->nombre && strncmp(n->nombre, prefijo, strlen(prefijo)) == 0;
}

static const char *lexema_de(const Nodo *n) {
  if (!n) return "";
  if (n->token && n->token->lexema) return n->token->lexema;
  return n->nombre ? n->nombre : "";
}

static TipoDato tipo_desde_nodo(const Nodo *n) {
  if (!n) return TIPO_ERROR;
  if (n->token) {
    switch (n->token->tipo) {
      case KW_INT:   return TIPO_INT;
      case KW_FLOAT: return TIPO_FLOAT;
      case KW_CHAR:  return TIPO_CHAR;
      case KW_BOOL:  return TIPO_BOOL;
      default:       break;
    }
  }
  if (n->nhijos == 1) return tipo_desde_nodo(n->hijos[0]);
  return TIPO_ERROR;
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

/* ============================================================================
 * Matriz de Compatibilidad de Tipos (Sección 6.2)
 * ============================================================================ */
TipoDato tipo_aritmetico_binario(const char *op, TipoDato izq, TipoDato der) {
  if (izq == TIPO_ERROR || der == TIPO_ERROR) return TIPO_ERROR;
  if (strcmp(op, "%") == 0) {
    return (izq == TIPO_INT && der == TIPO_INT) ? TIPO_INT : TIPO_ERROR;
  }
  bool izq_num = (izq == TIPO_INT || izq == TIPO_FLOAT);
  bool der_num = (der == TIPO_INT || der == TIPO_FLOAT);
  if (!izq_num || !der_num) return TIPO_ERROR;
  return (izq == TIPO_FLOAT || der == TIPO_FLOAT) ? TIPO_FLOAT : TIPO_INT;
}

TipoDato tipo_relacional(const char *op, TipoDato izq, TipoDato der) {
  if (izq == TIPO_ERROR || der == TIPO_ERROR) return TIPO_ERROR;
  bool es_eq = (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0);
  bool izq_num = (izq == TIPO_INT || izq == TIPO_FLOAT);
  bool der_num = (der == TIPO_INT || der == TIPO_FLOAT);
  if (izq_num && der_num) return TIPO_BOOL;
  if (izq == TIPO_CHAR && der == TIPO_CHAR) return TIPO_BOOL;
  if (es_eq && izq == TIPO_BOOL && der == TIPO_BOOL) return TIPO_BOOL;
  return TIPO_ERROR;
}

TipoDato tipo_logico_binario(TipoDato izq, TipoDato der) {
  if (izq == TIPO_ERROR || der == TIPO_ERROR) return TIPO_ERROR;
  return (izq == TIPO_BOOL && der == TIPO_BOOL) ? TIPO_BOOL : TIPO_ERROR;
}

bool asignacion_compatible(const char *op_asig, TipoDato destino, TipoDato valor) {
  if (destino == TIPO_ERROR || valor == TIPO_ERROR) return true;
  if (strcmp(op_asig, "=") == 0) {
    return (destino == valor) || (destino == TIPO_FLOAT && valor == TIPO_INT);
  }
  /* Compuestas: +=, -=, *=, /= */
  if (destino == TIPO_INT && valor == TIPO_INT) return true;
  if (destino == TIPO_FLOAT && (valor == TIPO_INT || valor == TIPO_FLOAT)) return true;
  return false;
}

/* ============================================================================
 * Prototipos internos
 * ============================================================================ */
static void visitar(SemCtx *ctx, Nodo *n);
static TipoDato eval_expr(SemCtx *ctx, Nodo *n);

static void aplanar_args(Nodo *n, Nodo **out, int *cnt, int max) {
  if (!n || *cnt >= max) return;
  if (es_nodo(n, "args_printf") || es_nodo(n, "args_scanf")) {
    for (int i = 0; i < n->nhijos; i++) {
      Nodo *h = n->hijos[i];
      if (h->token && h->token->tipo == DELIM_COMMA) continue;
      aplanar_args(h, out, cnt, max);
    }
    return;
  }
  out[(*cnt)++] = n;
}

/* ============================================================================
 * Validación de Entrada/Salida: sent_printf y sent_scanf (Sección 6.3)
 * ============================================================================ */
static void validar_sent_printf(SemCtx *ctx, Nodo *n) {
  if (n->nhijos < 6) return;
  Nodo *nodo_fmt = n->hijos[2];
  Nodo *args[64];
  int nargs = 0;
  aplanar_args(n->hijos[3], args, &nargs, 64);

  if (!nodo_fmt->token || nodo_fmt->token->tipo != LIT_STRING) {
    sem_err(ctx, n->linea, n->col,
            "printf: el primer argumento debe ser una cadena literal");
    return;
  }

  TipoDato tipos_arg[64];
  for (int i = 0; i < nargs; i++) {
    tipos_arg[i] = eval_expr(ctx, args[i]);
  }

  char fmt[1024];
  quitar_comillas(lexema_de(nodo_fmt), fmt, sizeof(fmt));

  int arg_i = 0;
  for (size_t i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] != '%') continue;
    i++;
    if (fmt[i] == '\0') {
      sem_err(ctx, n->linea, n->col,
              "printf: especificador de formato incompleto al final de la cadena");
      break;
    }
    if (fmt[i] == '%') continue;

    TipoDato esperado = TIPO_ERROR;
    switch (fmt[i]) {
      case 'd': esperado = TIPO_INT;    break;
      case 'f': esperado = TIPO_FLOAT;  break;
      case 'c': esperado = TIPO_CHAR;   break;
      case 's': esperado = TIPO_STRING; break;
      default:
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "printf: especificador de formato '%%%c' no soportado",
                       fmt[i]);
        arg_i++;
        continue;
    }

    if (arg_i >= nargs) {
      ctx->errores++;
      reportar_error(SEM, n->linea, n->col,
                     "printf: faltan argumentos para el especificador '%%%c'",
                     fmt[i]);
    } else if (tipos_arg[arg_i] != TIPO_ERROR) {
      /* Decisión A13: un bool se imprime con %d en printf. */
      bool compatible = (tipos_arg[arg_i] == esperado) ||
                        (fmt[i] == 'd' && tipos_arg[arg_i] == TIPO_BOOL);
      if (!compatible) {
        ctx->errores++;
        reportar_error(SEM, args[arg_i]->linea, args[arg_i]->col,
                       "printf: especificador '%%%c' espera '%s', pero el argumento %d es '%s'",
                       fmt[i], (fmt[i] == 'd') ? "int' o 'bool" : tipo_a_cadena(esperado),
                       arg_i + 1, tipo_a_cadena(tipos_arg[arg_i]));
      }
    }
    arg_i++;
  }

  if (arg_i < nargs) {
    ctx->errores++;
    reportar_error(SEM, n->linea, n->col,
                   "printf: sobran %d argumento(s) respecto a los especificadores de formato",
                   nargs - arg_i);
  }
}

static void validar_sent_scanf(SemCtx *ctx, Nodo *n) {
  if (n->nhijos < 6) return;
  Nodo *nodo_fmt = n->hijos[2];
  Nodo *args[64];
  int nargs = 0;
  aplanar_args(n->hijos[3], args, &nargs, 64);

  if (!nodo_fmt->token || nodo_fmt->token->tipo != LIT_STRING) {
    sem_err(ctx, n->linea, n->col,
            "scanf: el primer argumento debe ser una cadena literal");
    return;
  }

  char fmt[1024];
  quitar_comillas(lexema_de(nodo_fmt), fmt, sizeof(fmt));

  int arg_i = 0;
  for (size_t i = 0; fmt[i] != '\0'; i++) {
    if (fmt[i] != '%') continue;
    i++;
    if (fmt[i] == '\0') {
      sem_err(ctx, n->linea, n->col,
              "scanf: especificador de formato incompleto al final de la cadena");
      break;
    }
    if (fmt[i] == '%') continue;

    TipoDato esperado = TIPO_ERROR;
    switch (fmt[i]) {
      case 'd': esperado = TIPO_INT;   break;
      case 'f': esperado = TIPO_FLOAT; break;
      case 'c': esperado = TIPO_CHAR;  break;
      case 's':
        sem_err(ctx, n->linea, n->col,
                "scanf: el especificador '%s' no está soportado (sin arreglos ni punteros)");
        arg_i++;
        continue;
      default:
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "scanf: especificador de formato '%%%c' no soportado",
                       fmt[i]);
        arg_i++;
        continue;
    }

    if (arg_i >= nargs) {
      ctx->errores++;
      reportar_error(SEM, n->linea, n->col,
                     "scanf: faltan argumentos para el especificador '%%%c'",
                     fmt[i]);
      arg_i++;
      continue;
    }

    Nodo *arg = args[arg_i];
    if (!es_nodo(arg, "arg_dir") || arg->nhijos != 2) {
      ctx->errores++;
      reportar_error(SEM, arg->linea, arg->col,
                     "scanf: el argumento %d requiere el operador de dirección '&' sobre una variable",
                     arg_i + 1);
      /* Evaluamos si era un identificador para reportar también si no existe */
      if (arg->token && arg->token->tipo == IDENT &&
          !symtab_lookup(&ctx->ts, lexema_de(arg))) {
        ctx->errores++;
        reportar_error(SEM, arg->linea, arg->col,
                       "scanf: variable '%s' no declarada", lexema_de(arg));
      }
    } else {
      Nodo *nodo_id = arg->hijos[1];
      const char *var = lexema_de(nodo_id);
      Simbolo *sim = symtab_lookup(&ctx->ts, var);
      if (!sim) {
        ctx->errores++;
        reportar_error(SEM, nodo_id->linea, nodo_id->col,
                       "scanf: variable '%s' no declarada", var);
      } else if (sim->tipo != esperado) {
        ctx->errores++;
        reportar_error(SEM, nodo_id->linea, nodo_id->col,
                       "scanf: especificador '%%%c' espera '%s', pero '%s' es '%s'",
                       fmt[i], tipo_a_cadena(esperado), var,
                       tipo_a_cadena(sim->tipo));
      } else {
        nodo_id->tipo = sim->tipo;
        arg->tipo = sim->tipo;
      }
    }
    arg_i++;
  }

  if (arg_i < nargs) {
    ctx->errores++;
    reportar_error(SEM, n->linea, n->col,
                   "scanf: sobran %d argumento(s) respecto a los especificadores de formato",
                   nargs - arg_i);
  }
}

/* ============================================================================
 * Evaluación de Expresiones (exp_*)
 * ============================================================================ */
static TipoDato eval_expr(SemCtx *ctx, Nodo *n) {
  if (!n) return TIPO_ERROR;

  if (n->token) {
    switch (n->token->tipo) {
      case LIT_INT:    n->tipo = TIPO_INT;    return TIPO_INT;
      case LIT_FLOAT:  n->tipo = TIPO_FLOAT;  return TIPO_FLOAT;
      case LIT_CHAR:   n->tipo = TIPO_CHAR;   return TIPO_CHAR;
      case LIT_BOOL:   n->tipo = TIPO_BOOL;   return TIPO_BOOL;
      case LIT_STRING: n->tipo = TIPO_STRING; return TIPO_STRING;
      case IDENT: {
        const char *var = lexema_de(n);
        Simbolo *sim = symtab_lookup(&ctx->ts, var);
        if (!sim) {
          ctx->errores++;
          reportar_error(SEM, n->linea, n->col,
                         "variable '%s' no declarada", var);
          n->tipo = TIPO_ERROR;
          return TIPO_ERROR;
        }
        n->tipo = sim->tipo;
        return sim->tipo;
      }
      default:
        n->tipo = TIPO_ERROR;
        return TIPO_ERROR;
    }
  }

  if (es_nodo(n, "exp_parentesis") && n->nhijos == 3) {
    TipoDato t = eval_expr(ctx, n->hijos[1]);
    n->tipo = t;
    return t;
  }

  if (n->nhijos == 1) {
    TipoDato t = eval_expr(ctx, n->hijos[0]);
    n->tipo = t;
    return t;
  }

  /* Unarias: [op, expr] */
  if (n->nhijos == 2) {
    const char *op = lexema_de(n->hijos[0]);
    TipoDato t = eval_expr(ctx, n->hijos[1]);
    if (t == TIPO_ERROR) {
      n->tipo = TIPO_ERROR;
      return TIPO_ERROR;
    }

    if (strcmp(op, "-") == 0 || strcmp(op, "+") == 0) {
      if (t == TIPO_INT || t == TIPO_FLOAT) {
        n->tipo = t;
        return t;
      }
      ctx->errores++;
      reportar_error(SEM, n->linea, n->col,
                     "operador unario '%s' no aplicable a tipo '%s'",
                     op, tipo_a_cadena(t));
      n->tipo = TIPO_ERROR;
      return TIPO_ERROR;
    }

    if (strcmp(op, "!") == 0) {
      if (t == TIPO_BOOL) {
        n->tipo = TIPO_BOOL;
        return TIPO_BOOL;
      }
      ctx->errores++;
      reportar_error(SEM, n->linea, n->col,
                     "operador lógico '!' requiere 'bool', se obtuvo '%s'",
                     tipo_a_cadena(t));
      n->tipo = TIPO_ERROR;
      return TIPO_ERROR;
    }
    n->tipo = TIPO_ERROR;
    return TIPO_ERROR;
  }

  /* Binarias y asignaciones: [izq, op, der] */
  if (n->nhijos == 3) {
    const char *op = lexema_de(n->hijos[1]);

    if (es_nodo(n, "exp_asignacion") ||
        strcmp(op, "=") == 0 || strcmp(op, "+=") == 0 ||
        strcmp(op, "-=") == 0 || strcmp(op, "*=") == 0 || strcmp(op, "/=") == 0) {
      TipoDato t_dest = eval_expr(ctx, n->hijos[0]);
      TipoDato t_val  = eval_expr(ctx, n->hijos[2]);
      if (t_dest == TIPO_ERROR || t_val == TIPO_ERROR) {
        n->tipo = TIPO_ERROR;
        return TIPO_ERROR;
      }
      if (!asignacion_compatible(op, t_dest, t_val)) {
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "asignación incompatible (%s): no se puede asignar '%s' a '%s'",
                       op, tipo_a_cadena(t_val), tipo_a_cadena(t_dest));
        n->tipo = TIPO_ERROR;
        return TIPO_ERROR;
      }
      n->tipo = t_dest;
      return t_dest;
    }

    TipoDato izq = eval_expr(ctx, n->hijos[0]);
    TipoDato der = eval_expr(ctx, n->hijos[2]);
    if (izq == TIPO_ERROR || der == TIPO_ERROR) {
      n->tipo = TIPO_ERROR;
      return TIPO_ERROR;
    }

    if (strcmp(op, "+") == 0 || strcmp(op, "-") == 0 ||
        strcmp(op, "*") == 0 || strcmp(op, "/") == 0 || strcmp(op, "%") == 0) {
      TipoDato res = tipo_aritmetico_binario(op, izq, der);
      if (res == TIPO_ERROR) {
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "operación aritmética '%s' incompatible entre '%s' y '%s'",
                       op, tipo_a_cadena(izq), tipo_a_cadena(der));
      }
      n->tipo = res;
      return res;
    }

    if (strcmp(op, "<") == 0  || strcmp(op, ">") == 0  ||
        strcmp(op, "<=") == 0 || strcmp(op, ">=") == 0 ||
        strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
      TipoDato res = tipo_relacional(op, izq, der);
      if (res == TIPO_ERROR) {
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "comparación '%s' incompatible entre '%s' y '%s'",
                       op, tipo_a_cadena(izq), tipo_a_cadena(der));
      }
      n->tipo = res;
      return res;
    }

    if (strcmp(op, "&&") == 0 || strcmp(op, "||") == 0) {
      TipoDato res = tipo_logico_binario(izq, der);
      if (res == TIPO_ERROR) {
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "operador lógico '%s' requiere operandos 'bool' (se obtuvo '%s' y '%s')",
                       op, tipo_a_cadena(izq), tipo_a_cadena(der));
      }
      n->tipo = res;
      return res;
    }
  }

  n->tipo = TIPO_ERROR;
  return TIPO_ERROR;
}

/* ============================================================================
 * Declaraciones: declaracion -> tipo, lista_declaradores, [;]
 *                declarador  -> IDENT  |  IDENT, =, expresion
 * ============================================================================ */
static void procesar_declaradores(SemCtx *ctx, TipoDato tipo_var, Nodo *n) {
  if (!n) return;
  if (es_nodo(n, "declarador") && n->nhijos >= 1) {
    Nodo *nodo_id = n->hijos[0];
    TipoDato t_init = TIPO_VOID;
    bool tiene_init = (n->nhijos == 3);
    if (tiene_init) {
      t_init = eval_expr(ctx, n->hijos[2]);
    }

    const char *nombre = lexema_de(nodo_id);
    Simbolo *prev = NULL;
    if (!symtab_insert(&ctx->ts, nombre, tipo_var, nodo_id->linea, nodo_id->col, &prev)) {
      ctx->errores++;
      reportar_error(SEM, nodo_id->linea, nodo_id->col,
                     "redeclaración de '%s' en el mismo ámbito (declarada previamente en %d:%d)",
                     nombre, prev ? prev->linea : 0, prev ? prev->col : 0);
      return;
    }

    nodo_id->tipo = tipo_var;
    n->tipo = tipo_var;

    if (tiene_init && t_init != TIPO_ERROR &&
        !asignacion_compatible("=", tipo_var, t_init)) {
      ctx->errores++;
      reportar_error(SEM, nodo_id->linea, nodo_id->col,
                     "inicialización incompatible para '%s': se esperaba '%s' y se obtuvo '%s'",
                     nombre, tipo_a_cadena(tipo_var), tipo_a_cadena(t_init));
    }
    return;
  }

  for (int i = 0; i < n->nhijos; i++) {
    Nodo *h = n->hijos[i];
    if (h->token && h->token->tipo == DELIM_COMMA) continue;
    procesar_declaradores(ctx, tipo_var, h);
  }
}

/* ============================================================================
 * Recorrido del AST según el Contrato de Nodos
 * ============================================================================ */
static void visitar(SemCtx *ctx, Nodo *n) {
  if (!n) return;

  if (es_nodo(n, "programa") && n->nhijos == 6) {
    const char *fn = lexema_de(n->hijos[2]);
    if (strcmp(fn, "main") != 0) {
      ctx->errores++;
      reportar_error(SEM, n->hijos[2]->linea, n->hijos[2]->col,
                     "la función principal obligatoria es 'main', se encontró '%s'", fn);
    }
    visitar(ctx, n->hijos[5]);
    return;
  }

  if (es_nodo(n, "bloque") && n->nhijos >= 2) {
    symtab_enter_scope(&ctx->ts);
    visitar(ctx, n->hijos[1]);
    symtab_exit_scope(&ctx->ts);
    return;
  }

  if (es_nodo(n, "declaracion") && n->nhijos >= 2) {
    TipoDato tipo_var = tipo_desde_nodo(n->hijos[0]);
    procesar_declaradores(ctx, tipo_var, n->hijos[1]);
    return;
  }

  if (es_nodo(n, "sent_expresion") && n->nhijos >= 1) {
    eval_expr(ctx, n->hijos[0]);
    return;
  }

  if (es_nodo(n, "sent_if") && n->nhijos == 5) {
    TipoDato tc = eval_expr(ctx, n->hijos[2]);
    if (tc != TIPO_ERROR && tc != TIPO_BOOL) {
      ctx->errores++;
      reportar_error(SEM, n->hijos[2]->linea, n->hijos[2]->col,
                     "la condición de 'if' debe ser de tipo 'bool', se obtuvo '%s'",
                     tipo_a_cadena(tc));
    }
    visitar(ctx, n->hijos[4]);
    return;
  }

  if (es_nodo(n, "sent_if_else") && n->nhijos == 7) {
    TipoDato tc = eval_expr(ctx, n->hijos[2]);
    if (tc != TIPO_ERROR && tc != TIPO_BOOL) {
      ctx->errores++;
      reportar_error(SEM, n->hijos[2]->linea, n->hijos[2]->col,
                     "la condición de 'if' debe ser de tipo 'bool', se obtuvo '%s'",
                     tipo_a_cadena(tc));
    }
    visitar(ctx, n->hijos[4]);
    visitar(ctx, n->hijos[6]);
    return;
  }

  if (es_nodo(n, "sent_while") && n->nhijos == 5) {
    TipoDato tc = eval_expr(ctx, n->hijos[2]);
    if (tc != TIPO_ERROR && tc != TIPO_BOOL) {
      ctx->errores++;
      reportar_error(SEM, n->hijos[2]->linea, n->hijos[2]->col,
                     "la condición de 'while' debe ser de tipo 'bool', se obtuvo '%s'",
                     tipo_a_cadena(tc));
    }
    visitar(ctx, n->hijos[4]);
    return;
  }

  if (es_nodo(n, "sent_for") && n->nhijos == 9) {
    symtab_enter_scope(&ctx->ts);
    if (n->hijos[2] && n->hijos[2]->nhijos > 0) {
      Nodo *init = n->hijos[2]->hijos[0];
      if (es_nodo(init, "declaracion"))
        visitar(ctx, init);
      else
        eval_expr(ctx, init);
    }
    if (n->hijos[4] && n->hijos[4]->nhijos > 0) {
      TipoDato tc = eval_expr(ctx, n->hijos[4]->hijos[0]);
      if (tc != TIPO_ERROR && tc != TIPO_BOOL) {
        ctx->errores++;
        reportar_error(SEM, n->hijos[4]->linea, n->hijos[4]->col,
                       "la condición de 'for' debe ser de tipo 'bool', se obtuvo '%s'",
                       tipo_a_cadena(tc));
      }
    }
    if (n->hijos[6] && n->hijos[6]->nhijos > 0) eval_expr(ctx, n->hijos[6]->hijos[0]);
    visitar(ctx, n->hijos[8]);
    symtab_exit_scope(&ctx->ts);
    return;
  }

  if (es_nodo(n, "sent_printf")) { validar_sent_printf(ctx, n); return; }
  if (es_nodo(n, "sent_scanf"))  { validar_sent_scanf(ctx, n);  return; }

  if (es_nodo(n, "sent_return")) {
    if (n->nhijos == 3) {
      TipoDato t = eval_expr(ctx, n->hijos[1]);
      if (t != TIPO_ERROR && t != TIPO_INT) {
        ctx->errores++;
        reportar_error(SEM, n->linea, n->col,
                       "return en 'int main()' debe devolver 'int', se obtuvo '%s'",
                       tipo_a_cadena(t));
      }
    } else {
      sem_err(ctx, n->linea, n->col,
              "return en 'int main()' debe devolver un valor de tipo 'int'");
    }
    return;
  }

  if (empieza_con(n, "exp_")) {
    eval_expr(ctx, n);
    return;
  }

  for (int i = 0; i < n->nhijos; i++) {
    visitar(ctx, n->hijos[i]);
  }
}

int analizar_semantica(Nodo *raiz, FILE *out) {
  SemCtx ctx;
  symtab_init(&ctx.ts);
  ctx.errores = 0;

  if (raiz) {
    symtab_enter_scope(&ctx.ts);
    visitar(&ctx, raiz);
    symtab_exit_scope(&ctx.ts);
  }

  symtab_free(&ctx.ts);

  if (ctx.errores == 0 && raiz && out) {
    interpretar_simbolico(raiz, out);
  }

  return ctx.errores;
}
