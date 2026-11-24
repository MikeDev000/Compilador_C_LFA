/*
 * parser.y — Fase 2: análisis sintáctico (Dueño: Dev 2).
 *
 * Gramática Libre de Contexto (GLC) no ambigua y LALR(1) (%expect 0) para el
 * subconjunto de C de ccheck:
 *   - Directivas #include y función principal int main() { ... }
 *   - Declaraciones (int, float, char, bool) con lista de declaradores e init
 *   - Estructuras de control: if/else (dangling-else resuelto con matched/unmatched),
 *     while y for (con declaración local opcional en la inicialización)
 *   - Expresiones estratificadas por precedencia y asociatividad
 *   - Llamadas a printf() y scanf() (& restringido a argumentos de scanf)
 *   - Sentencia return
 */
%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "token.h"
#include "ast.h"
#include "diag.h"

int yylex(void);
void yyerror(const char *s);

Nodo *raiz_ast = NULL;

/* Helpers internos para construir nodos de forma limpia y consistente. */
static Nodo *nodo_binario(const char *nombre, Nodo *izq, Token *op, Nodo *der) {
  Nodo *n = nodo_nuevo(nombre, izq->linea, izq->col);
  nodo_agregar(n, izq);
  nodo_agregar(n, nodo_hoja(op));
  nodo_agregar(n, der);
  return n;
}

static Nodo *nodo_unario(const char *nombre, Token *op, Nodo *expr) {
  Nodo *n = nodo_nuevo(nombre, op->linea, op->columna);
  nodo_agregar(n, nodo_hoja(op));
  nodo_agregar(n, expr);
  return n;
}

static Nodo *nodo_if_else(Token *kw_if, Token *lp, Nodo *cond, Token *rp,
                          Nodo *rama_then, Token *kw_else, Nodo *rama_else) {
  Nodo *n = nodo_nuevo("sent_if_else", kw_if->linea, kw_if->columna);
  nodo_agregar(n, nodo_hoja(kw_if));
  nodo_agregar(n, nodo_hoja(lp));
  nodo_agregar(n, cond);
  nodo_agregar(n, nodo_hoja(rp));
  nodo_agregar(n, rama_then);
  nodo_agregar(n, nodo_hoja(kw_else));
  nodo_agregar(n, rama_else);
  return n;
}

static Nodo *nodo_while(Token *kw_w, Token *lp, Nodo *cond, Token *rp,
                        Nodo *cuerpo) {
  Nodo *n = nodo_nuevo("sent_while", kw_w->linea, kw_w->columna);
  nodo_agregar(n, nodo_hoja(kw_w));
  nodo_agregar(n, nodo_hoja(lp));
  nodo_agregar(n, cond);
  nodo_agregar(n, nodo_hoja(rp));
  nodo_agregar(n, cuerpo);
  return n;
}

static Nodo *nodo_for(Token *kw_f, Token *lp, Nodo *init, Token *s1,
                      Nodo *cond, Token *s2, Nodo *act, Token *rp,
                      Nodo *cuerpo) {
  Nodo *n = nodo_nuevo("sent_for", kw_f->linea, kw_f->columna);
  nodo_agregar(n, nodo_hoja(kw_f));
  nodo_agregar(n, nodo_hoja(lp));
  nodo_agregar(n, init);
  nodo_agregar(n, nodo_hoja(s1));
  nodo_agregar(n, cond);
  nodo_agregar(n, nodo_hoja(s2));
  nodo_agregar(n, act);
  nodo_agregar(n, nodo_hoja(rp));
  nodo_agregar(n, cuerpo);
  return n;
}
%}

%code requires {
  struct Token;
  struct Nodo;
}

%locations
%define parse.error custom
%define parse.lac full
%define parse.trace
%expect 0

%union {
  struct Token *tok;
  struct Nodo *nodo;
}

/* ===================== LISTA ÚNICA DE TOKENS (Dev 1 + Dev 2) ============= */
%token <tok> KW_INT     "'int'"
%token <tok> KW_FLOAT   "'float'"
%token <tok> KW_CHAR    "'char'"
%token <tok> KW_BOOL    "'bool'"
%token <tok> KW_IF      "'if'"
%token <tok> KW_ELSE    "'else'"
%token <tok> KW_WHILE   "'while'"
%token <tok> KW_FOR     "'for'"
%token <tok> KW_SCANF   "'scanf'"
%token <tok> KW_PRINTF  "'printf'"
%token <tok> KW_RETURN  "'return'"

%token <tok> IDENT      "identificador"
%token <tok> LIT_INT    "literal entero"
%token <tok> LIT_FLOAT  "literal decimal"
%token <tok> LIT_CHAR   "literal char"
%token <tok> LIT_STRING "cadena"
%token <tok> LIT_BOOL   "literal bool"
%token <tok> PP_INCLUDE "#include"

%token <tok> OP_PLUS    "'+'"
%token <tok> OP_MINUS   "'-'"
%token <tok> OP_MUL     "'*'"
%token <tok> OP_DIV     "'/'"
%token <tok> OP_MOD     "'%'"
%token <tok> OP_ASSIGN      "'='"
%token <tok> OP_ADD_ASSIGN  "'+='"
%token <tok> OP_SUB_ASSIGN  "'-='"
%token <tok> OP_MUL_ASSIGN  "'*='"
%token <tok> OP_DIV_ASSIGN  "'/='"
%token <tok> OP_EQ      "'=='"
%token <tok> OP_NE      "'!='"
%token <tok> OP_LT      "'<'"
%token <tok> OP_GT      "'>'"
%token <tok> OP_LE      "'<='"
%token <tok> OP_GE      "'>='"
%token <tok> OP_AND     "'&&'"
%token <tok> OP_OR      "'||'"
%token <tok> OP_NOT     "'!'"
%token <tok> OP_AMP     "'&'"

%token <tok> DELIM_SEMI   "';'"
%token <tok> DELIM_COMMA  "','"
%token <tok> DELIM_LPAREN "'('"
%token <tok> DELIM_RPAREN "')'"
%token <tok> DELIM_LBRACE "'{'"
%token <tok> DELIM_RBRACE "'}'"
/* ========================================================================= */

%type <nodo> programa includes bloque lista_elementos elemento
%type <nodo> declaracion declaracion_sin_pyc tipo lista_declaradores declarador
%type <nodo> sentencia sent_emparejada sent_no_emparejada sent_simple
%type <nodo> sent_expresion sent_return
%type <nodo> sent_printf opt_args_printf lista_args_printf arg_printf
%type <nodo> sent_scanf opt_args_scanf lista_args_scanf arg_scanf
%type <nodo> for_init for_cond for_act
%type <nodo> expresion exp_asignacion op_asignacion
%type <nodo> exp_or exp_and exp_igualdad exp_relacional
%type <nodo> exp_aditiva exp_multiplicativa exp_unaria exp_primaria

%destructor { if ($$ != raiz_ast) nodo_liberar($$); } <nodo>

%start programa

%%

/* ======================== Estructura del programa ======================== */

programa
  : includes KW_INT IDENT DELIM_LPAREN DELIM_RPAREN bloque {
      $$ = nodo_nuevo("programa", @2.first_line, @2.first_column);
      nodo_agregar($$, $1);
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, nodo_hoja($3));
      nodo_agregar($$, nodo_hoja($4));
      nodo_agregar($$, nodo_hoja($5));
      nodo_agregar($$, $6);
      raiz_ast = $$;
    }
  ;

includes
  : %empty              { $$ = nodo_nuevo("includes", 1, 1); }
  | includes PP_INCLUDE { $$ = $1; nodo_agregar($$, nodo_hoja($2)); }
  ;

bloque
  : DELIM_LBRACE lista_elementos DELIM_RBRACE {
      $$ = nodo_nuevo("bloque", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, $2);
      nodo_agregar($$, nodo_hoja($3));
    }
  ;

lista_elementos
  : %empty                   { $$ = nodo_nuevo("lista_elementos", 0, 0); }
  | lista_elementos elemento { $$ = $1; nodo_agregar($$, $2); }
  ;

elemento
  : declaracion { $$ = $1; }
  | sentencia   { $$ = $1; }
  ;

/* ============================= Declaraciones ============================= */

declaracion
  : declaracion_sin_pyc DELIM_SEMI {
      $$ = $1;
      nodo_agregar($$, nodo_hoja($2));
    }
  ;

declaracion_sin_pyc
  : tipo lista_declaradores {
      $$ = nodo_nuevo("declaracion", @1.first_line, @1.first_column);
      nodo_agregar($$, $1);
      nodo_agregar($$, $2);
    }
  ;

tipo
  : KW_INT   { $$ = nodo_nuevo("tipo", @1.first_line, @1.first_column); nodo_agregar($$, nodo_hoja($1)); }
  | KW_FLOAT { $$ = nodo_nuevo("tipo", @1.first_line, @1.first_column); nodo_agregar($$, nodo_hoja($1)); }
  | KW_CHAR  { $$ = nodo_nuevo("tipo", @1.first_line, @1.first_column); nodo_agregar($$, nodo_hoja($1)); }
  | KW_BOOL  { $$ = nodo_nuevo("tipo", @1.first_line, @1.first_column); nodo_agregar($$, nodo_hoja($1)); }
  ;

lista_declaradores
  : declarador {
      $$ = nodo_nuevo("lista_declaradores", @1.first_line, @1.first_column);
      nodo_agregar($$, $1);
    }
  | lista_declaradores DELIM_COMMA declarador {
      $$ = $1;
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, $3);
    }
  ;

declarador
  : IDENT {
      $$ = nodo_nuevo("declarador", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
    }
  | IDENT OP_ASSIGN expresion {
      $$ = nodo_nuevo("declarador", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, $3);
    }
  ;

/* ============== Sentencias (resolución formal de Dangling-Else) ========== */

sentencia
  : sent_emparejada    { $$ = $1; }
  | sent_no_emparejada { $$ = $1; }
  ;

sent_emparejada
  : KW_IF DELIM_LPAREN expresion DELIM_RPAREN sent_emparejada KW_ELSE sent_emparejada {
      $$ = nodo_if_else($1, $2, $3, $4, $5, $6, $7);
    }
  | KW_WHILE DELIM_LPAREN expresion DELIM_RPAREN sent_emparejada {
      $$ = nodo_while($1, $2, $3, $4, $5);
    }
  | KW_FOR DELIM_LPAREN for_init DELIM_SEMI for_cond DELIM_SEMI for_act DELIM_RPAREN sent_emparejada {
      $$ = nodo_for($1, $2, $3, $4, $5, $6, $7, $8, $9);
    }
  | sent_simple { $$ = $1; }
  ;

sent_no_emparejada
  : KW_IF DELIM_LPAREN expresion DELIM_RPAREN sentencia {
      $$ = nodo_nuevo("sent_if", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, $3);
      nodo_agregar($$, nodo_hoja($4));
      nodo_agregar($$, $5);
    }
  | KW_IF DELIM_LPAREN expresion DELIM_RPAREN sent_emparejada KW_ELSE sent_no_emparejada {
      $$ = nodo_if_else($1, $2, $3, $4, $5, $6, $7);
    }
  | KW_WHILE DELIM_LPAREN expresion DELIM_RPAREN sent_no_emparejada {
      $$ = nodo_while($1, $2, $3, $4, $5);
    }
  | KW_FOR DELIM_LPAREN for_init DELIM_SEMI for_cond DELIM_SEMI for_act DELIM_RPAREN sent_no_emparejada {
      $$ = nodo_for($1, $2, $3, $4, $5, $6, $7, $8, $9);
    }
  ;

sent_simple
  : bloque         { $$ = $1; }
  | sent_expresion { $$ = $1; }
  | sent_printf    { $$ = $1; }
  | sent_scanf     { $$ = $1; }
  | sent_return    { $$ = $1; }
  ;

for_init
  : %empty              { $$ = nodo_nuevo("for_init", 0, 0); }
  | declaracion_sin_pyc { $$ = nodo_nuevo("for_init", @1.first_line, @1.first_column); nodo_agregar($$, $1); }
  | expresion           { $$ = nodo_nuevo("for_init", @1.first_line, @1.first_column); nodo_agregar($$, $1); }
  ;

for_cond
  : %empty    { $$ = nodo_nuevo("for_cond", 0, 0); }
  | expresion { $$ = nodo_nuevo("for_cond", @1.first_line, @1.first_column); nodo_agregar($$, $1); }
  ;

for_act
  : %empty    { $$ = nodo_nuevo("for_act", 0, 0); }
  | expresion { $$ = nodo_nuevo("for_act", @1.first_line, @1.first_column); nodo_agregar($$, $1); }
  ;

sent_expresion
  : expresion DELIM_SEMI {
      $$ = nodo_nuevo("sent_expresion", @1.first_line, @1.first_column);
      nodo_agregar($$, $1);
      nodo_agregar($$, nodo_hoja($2));
    }
  | DELIM_SEMI {
      $$ = nodo_nuevo("sent_vacia", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
    }
  ;

sent_return
  : KW_RETURN expresion DELIM_SEMI {
      $$ = nodo_nuevo("sent_return", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, $2);
      nodo_agregar($$, nodo_hoja($3));
    }
  | KW_RETURN DELIM_SEMI {
      $$ = nodo_nuevo("sent_return", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, nodo_hoja($2));
    }
  ;

/* ========================= Entrada / Salida (E/S) ======================== */

sent_printf
  : KW_PRINTF DELIM_LPAREN LIT_STRING opt_args_printf DELIM_RPAREN DELIM_SEMI {
      $$ = nodo_nuevo("sent_printf", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, nodo_hoja($3));
      nodo_agregar($$, $4);
      nodo_agregar($$, nodo_hoja($5));
      nodo_agregar($$, nodo_hoja($6));
    }
  ;

opt_args_printf
  : %empty                         { $$ = nodo_nuevo("args_printf", 0, 0); }
  | DELIM_COMMA lista_args_printf  { $$ = $2; }
  ;

lista_args_printf
  : arg_printf {
      $$ = nodo_nuevo("args_printf", @1.first_line, @1.first_column);
      nodo_agregar($$, $1);
    }
  | lista_args_printf DELIM_COMMA arg_printf {
      $$ = $1;
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, $3);
    }
  ;

arg_printf
  : expresion  { $$ = $1; }
  | LIT_STRING { $$ = nodo_hoja($1); }
  ;

sent_scanf
  : KW_SCANF DELIM_LPAREN LIT_STRING opt_args_scanf DELIM_RPAREN DELIM_SEMI {
      $$ = nodo_nuevo("sent_scanf", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, nodo_hoja($3));
      nodo_agregar($$, $4);
      nodo_agregar($$, nodo_hoja($5));
      nodo_agregar($$, nodo_hoja($6));
    }
  ;

opt_args_scanf
  : %empty                        { $$ = nodo_nuevo("args_scanf", 0, 0); }
  | DELIM_COMMA lista_args_scanf  { $$ = $2; }
  ;

lista_args_scanf
  : arg_scanf {
      $$ = nodo_nuevo("args_scanf", @1.first_line, @1.first_column);
      nodo_agregar($$, $1);
    }
  | lista_args_scanf DELIM_COMMA arg_scanf {
      $$ = $1;
      nodo_agregar($$, nodo_hoja($2));
      nodo_agregar($$, $3);
    }
  ;

arg_scanf
  : OP_AMP IDENT {
      $$ = nodo_nuevo("arg_dir", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, nodo_hoja($2));
    }
  | expresion { $$ = $1; }
  ;

/* ============== Expresiones (menor a mayor precedencia) ================== */

expresion
  : exp_asignacion { $$ = $1; }
  ;

/* Nivel 1 (menor precedencia): asignación, asociativa por la derecha. */
exp_asignacion
  : IDENT op_asignacion exp_asignacion {
      $$ = nodo_nuevo("exp_asignacion", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, $2);
      nodo_agregar($$, $3);
    }
  | exp_or { $$ = $1; }
  ;

op_asignacion
  : OP_ASSIGN     { $$ = nodo_hoja($1); }
  | OP_ADD_ASSIGN { $$ = nodo_hoja($1); }
  | OP_SUB_ASSIGN { $$ = nodo_hoja($1); }
  | OP_MUL_ASSIGN { $$ = nodo_hoja($1); }
  | OP_DIV_ASSIGN { $$ = nodo_hoja($1); }
  ;

/* Nivel 2: OR lógico (||), asociativo por la izquierda. */
exp_or
  : exp_or OP_OR exp_and { $$ = nodo_binario("exp_or", $1, $2, $3); }
  | exp_and              { $$ = $1; }
  ;

/* Nivel 3: AND lógico (&&), asociativo por la izquierda. */
exp_and
  : exp_and OP_AND exp_igualdad { $$ = nodo_binario("exp_and", $1, $2, $3); }
  | exp_igualdad                { $$ = $1; }
  ;

/* Nivel 4: Igualdad (==, !=), asociativo por la izquierda. */
exp_igualdad
  : exp_igualdad OP_EQ exp_relacional { $$ = nodo_binario("exp_igualdad", $1, $2, $3); }
  | exp_igualdad OP_NE exp_relacional { $$ = nodo_binario("exp_igualdad", $1, $2, $3); }
  | exp_relacional                    { $$ = $1; }
  ;

/* Nivel 5: Relacionales (<, <=, >, >=), asociativo por la izquierda. */
exp_relacional
  : exp_relacional OP_LT exp_aditiva { $$ = nodo_binario("exp_relacional", $1, $2, $3); }
  | exp_relacional OP_LE exp_aditiva { $$ = nodo_binario("exp_relacional", $1, $2, $3); }
  | exp_relacional OP_GT exp_aditiva { $$ = nodo_binario("exp_relacional", $1, $2, $3); }
  | exp_relacional OP_GE exp_aditiva { $$ = nodo_binario("exp_relacional", $1, $2, $3); }
  | exp_aditiva                      { $$ = $1; }
  ;

/* Nivel 6: Aditivos (+, -), asociativo por la izquierda. */
exp_aditiva
  : exp_aditiva OP_PLUS exp_multiplicativa  { $$ = nodo_binario("exp_aditiva", $1, $2, $3); }
  | exp_aditiva OP_MINUS exp_multiplicativa { $$ = nodo_binario("exp_aditiva", $1, $2, $3); }
  | exp_multiplicativa                      { $$ = $1; }
  ;

/* Nivel 7: Multiplicativos (*, /, %), asociativo por la izquierda. */
exp_multiplicativa
  : exp_multiplicativa OP_MUL exp_unaria { $$ = nodo_binario("exp_multiplicativa", $1, $2, $3); }
  | exp_multiplicativa OP_DIV exp_unaria { $$ = nodo_binario("exp_multiplicativa", $1, $2, $3); }
  | exp_multiplicativa OP_MOD exp_unaria { $$ = nodo_binario("exp_multiplicativa", $1, $2, $3); }
  | exp_unaria                           { $$ = $1; }
  ;

/* Nivel 8: Unarios (!, -, +), asociativo por la derecha. */
exp_unaria
  : OP_NOT exp_unaria   { $$ = nodo_unario("exp_unaria", $1, $2); }
  | OP_MINUS exp_unaria { $$ = nodo_unario("exp_unaria", $1, $2); }
  | OP_PLUS exp_unaria  { $$ = nodo_unario("exp_unaria", $1, $2); }
  | exp_primaria        { $$ = $1; }
  ;

/* Nivel 9 (mayor precedencia): expresiones primarias y paréntesis. */
exp_primaria
  : IDENT     { $$ = nodo_hoja($1); }
  | LIT_INT   { $$ = nodo_hoja($1); }
  | LIT_FLOAT { $$ = nodo_hoja($1); }
  | LIT_CHAR  { $$ = nodo_hoja($1); }
  | LIT_BOOL  { $$ = nodo_hoja($1); }
  | DELIM_LPAREN expresion DELIM_RPAREN {
      $$ = nodo_nuevo("exp_parentesis", @1.first_line, @1.first_column);
      nodo_agregar($$, nodo_hoja($1));
      nodo_agregar($$, $2);
      nodo_agregar($$, nodo_hoja($3));
    }
  ;

%%

/* ------------------------- Errores sintácticos ------------------------- */

static void nombre_simbolo(yysymbol_kind_t k, char *buf, size_t n) {
  const char *s = (k == YYSYMBOL_YYEOF) ? "fin de archivo" : yysymbol_name(k);
  size_t len = strlen(s);
  if (len >= 2 && s[0] == '"' && s[len - 1] == '"') { s++; len -= 2; }
  if (len >= n) len = n - 1;
  memcpy(buf, s, len);
  buf[len] = '\0';
}

static int yyreport_syntax_error(const yypcontext_t *ctx) {
  enum { MAX_ESP = 4 };
  yysymbol_kind_t esperados[YYNTOKENS];
  int total = yypcontext_expected_tokens(ctx, esperados, YYNTOKENS);
  int n = total < MAX_ESP ? total : MAX_ESP;
  const YYLTYPE *loc = yypcontext_location(ctx);

  char lista[256] = "";
  for (int i = 0; i < n; i++) {
    char nom[64];
    nombre_simbolo(esperados[i], nom, sizeof nom);
    if (i > 0) strncat(lista, (i == n - 1 && total <= MAX_ESP) ? " o " : ", ",
                       sizeof lista - strlen(lista) - 1);
    strncat(lista, nom, sizeof lista - strlen(lista) - 1);
  }
  if (total > MAX_ESP) strncat(lista, ", ...", sizeof lista - strlen(lista) - 1);

  char encontrado[80];
  if (yypcontext_token(ctx) == YYSYMBOL_YYEOF || !token_actual)
    snprintf(encontrado, sizeof encontrado, "fin de archivo");
  else
    snprintf(encontrado, sizeof encontrado, "'%s'", token_actual->lexema);

  if (n > 0)
    reportar_error(SYN, loc->first_line, loc->first_column,
                   "se esperaba %s y se encontró %s", lista, encontrado);
  else
    reportar_error(SYN, loc->first_line, loc->first_column,
                   "token inesperado %s", encontrado);
  return 0;
}

void yyerror(const char *s) {
  reportar_error(SYN, yylloc.first_line, yylloc.first_column, "%s", s);
}

/* ----------------------- Construcción del árbol ------------------------ */

Nodo *nodo_nuevo(const char *nombre, int linea, int col) {
  Nodo *n = calloc(1, sizeof *n);
  if (!n) { fprintf(stderr, "ccheck: sin memoria\n"); exit(EXIT_FAILURE); }
  n->nombre = malloc(strlen(nombre) + 1);
  if (!n->nombre) { fprintf(stderr, "ccheck: sin memoria\n"); exit(EXIT_FAILURE); }
  strcpy(n->nombre, nombre);
  n->linea = linea;
  n->col = col;
  return n;
}

Nodo *nodo_hoja(Token *t) {
  Nodo *n = nodo_nuevo(nombre_token(t->tipo), t->linea, t->columna);
  n->token = t;
  return n;
}

void nodo_agregar(Nodo *padre, Nodo *hijo) {
  if (!padre || !hijo) return;
  if (padre->nhijos == padre->cap) {
    int cap = padre->cap ? padre->cap * 2 : 4;
    Nodo **h = realloc(padre->hijos, (size_t)cap * sizeof *h);
    if (!h) { fprintf(stderr, "ccheck: sin memoria\n"); exit(EXIT_FAILURE); }
    padre->hijos = h;
    padre->cap = cap;
  }
  padre->hijos[padre->nhijos++] = hijo;
}

void nodo_liberar(Nodo *n) {
  if (!n) return;
  for (int i = 0; i < n->nhijos; i++) nodo_liberar(n->hijos[i]);
  free(n->hijos);
  free(n->nombre);
  free(n);
}