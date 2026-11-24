/*
 * main.c — Driver de ccheck (Dueño: Dev 1).
 *
 * Uso:   ccheck [-o dir_salida] entrada.c
 *
 * Una sola ejecución (A12) corre lex → parse → sem con corte por fase (A8)
 * y escribe en dir_salida (por defecto "salidas/"):
 *     tokens.txt   tabla de tokens (Fase 1)
 *     arbol.txt    árbol de derivación en texto (Fase 2)
 *     arbol.dot    árbol en Graphviz (Fase 2; PNG con `make dot`)
 *     errores.txt  errores de todas las fases + notas de fases omitidas
 *
 * Códigos de salida: 0 = sin errores · 1 = léxicos · 2 = sintácticos ·
 *                    3 = semánticos · 4 = error de uso o de E/S.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#include <direct.h>
#define CCHECK_MKDIR(ruta, modo) _mkdir(ruta)
#else
#define CCHECK_MKDIR(ruta, modo) mkdir((ruta), (modo))
#endif

#include "ast.h"
#include "diag.h"
#include "fases.h"
#include "token.h"

int yyparse(void);

enum {
  SALIDA_OK = 0,
  SALIDA_LEX = 1,
  SALIDA_SYN = 2,
  SALIDA_SEM = 3,
  SALIDA_USO = 4
};

static void uso(const char *prog) {
  fprintf(stderr,
          "uso: %s [-o dir_salida] [--tokens] [--tree [salida.dot]] [--sem] "
          "entrada.c\n",
          prog);
}

/* Crea el directorio y sus padres (como `mkdir -p`). */
static int crear_directorio(const char *dir) {
  char ruta[4096];
  snprintf(ruta, sizeof ruta, "%s", dir);
  for (char *p = ruta + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (CCHECK_MKDIR(ruta, 0755) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (CCHECK_MKDIR(ruta, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

static FILE *abrir_salida(const char *dir, const char *nombre) {
  char ruta[4096];
  snprintf(ruta, sizeof ruta, "%s/%s", dir, nombre);
  FILE *f = fopen(ruta, "w");
  if (!f) fprintf(stderr, "ccheck: no se pudo crear '%s': %s\n", ruta, strerror(errno));
  return f;
}

int main(int argc, char **argv) {
  const char *dir = "salidas";
  const char *entrada = NULL;
  const char *ruta_dot_extra = NULL;
  int flag_tokens = 0, flag_tree = 0, flag_sem = 0;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      dir = argv[++i];
    } else if (strcmp(argv[i], "--tokens") == 0) {
      flag_tokens = 1;
    } else if (strcmp(argv[i], "--tree") == 0) {
      flag_tree = 1;
      if (i + 1 < argc && argv[i + 1][0] != '-' &&
          strstr(argv[i + 1], ".dot") != NULL) {
        ruta_dot_extra = argv[++i];
      }
    } else if (strcmp(argv[i], "--sem") == 0) {
      flag_sem = 1;
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      uso(argv[0]);
      return SALIDA_OK;
    } else if (argv[i][0] == '-' || entrada) {
      uso(argv[0]);
      return SALIDA_USO;
    } else {
      entrada = argv[i];
    }
  }
  /* Sin banderas de fase, se ejecutan las 3 fases (decisión A12). */
  int max_fase = SEM;
  if (flag_tokens || flag_tree || flag_sem) {
    max_fase = flag_sem ? SEM : (flag_tree ? SYN : LEX);
  }
  if (!entrada) {
    uso(argv[0]);
    return SALIDA_USO;
  }

  FILE *src = fopen(entrada, "r");
  if (!src) {
    fprintf(stderr, "ccheck: no se pudo abrir '%s': %s\n", entrada, strerror(errno));
    return SALIDA_USO;
  }
  if (crear_directorio(dir) != 0) {
    fprintf(stderr, "ccheck: no se pudo crear el directorio '%s': %s\n", dir,
            strerror(errno));
    fclose(src);
    return SALIDA_USO;
  }

  FILE *f_tokens = abrir_salida(dir, "tokens.txt");
  FILE *f_arbol = abrir_salida(dir, "arbol.txt");
  FILE *f_err = abrir_salida(dir, "errores.txt");
  if (!f_tokens || !f_arbol || !f_err) {
    if (f_tokens) fclose(f_tokens);
    if (f_arbol) fclose(f_arbol);
    if (f_err) fclose(f_err);
    fclose(src);
    return SALIDA_USO;
  }

  diag_init(entrada, f_err);
  int codigo = SALIDA_OK;

  /* ------------------------- Fase 1: léxica ------------------------- */
  printf("== Fase 1: análisis léxico ==\n");
  lexer_iniciar(src);
  int n_lex = lexer_ejecutar();
  tokens_escribir(f_tokens);
  fclose(f_tokens); /* tokens.txt queda completo aunque una fase posterior falle */
  f_tokens = NULL;
  if (flag_tokens) tokens_escribir(stdout);
  printf("%zu tokens reconocidos, %d error(es) léxico(s) -> %s/tokens.txt\n",
         tabla_tokens.n, n_lex, dir);

  if (n_lex > 0) {
    diag_nota("Fases sintáctica y semántica omitidas por errores léxicos");
    fprintf(f_arbol, "Árbol no generado: fase sintáctica omitida por errores léxicos.\n");
    codigo = SALIDA_LEX;
    goto fin;
  }
  if (max_fase == LEX) {
    diag_nota("Fases sintáctica y semántica omitidas (--tokens)");
    fprintf(f_arbol, "Árbol no generado: ejecución limitada a Fase 1 (--tokens).\n");
    goto fin;
  }

  /* ----------------------- Fase 2: sintáctica ----------------------- */
  printf("== Fase 2: análisis sintáctico ==\n");
  int r = yyparse();
  if (r != 0 || diag_cuenta(SYN) > 0 || raiz_ast == NULL) {
    if (diag_cuenta(SYN) == 0) /* p. ej. memoria agotada en el parser */
      reportar_error(SYN, 1, 1, "el análisis sintáctico no pudo completarse");
    diag_nota("Fase semántica omitida por errores sintácticos");
    fprintf(f_arbol, "Árbol no generado: hay errores sintácticos.\n");
    codigo = SALIDA_SYN;
    goto fin;
  }
  arbol_imprimir_txt(raiz_ast, f_arbol);
  FILE *f_dot = abrir_salida(dir, "arbol.dot");
  if (f_dot) {
    arbol_exportar_dot(raiz_ast, f_dot);
    fclose(f_dot);
  }
  if (ruta_dot_extra) {
    FILE *f_extra = fopen(ruta_dot_extra, "w");
    if (f_extra) {
      arbol_exportar_dot(raiz_ast, f_extra);
      fclose(f_extra);
    } else {
      fprintf(stderr, "ccheck: no se pudo crear '%s': %s\n", ruta_dot_extra,
              strerror(errno));
    }
  }
  printf("Estructura válida -> %s/arbol.txt, %s/arbol.dot\n", dir, dir);
  if (max_fase == SYN) {
    diag_nota("Fase semántica omitida (--tree)");
    goto fin;
  }

  /* ----------------------- Fase 3: semántica ------------------------ */
  printf("== Fase 3: análisis semántico ==\n");
  int n_sem = analizar_semantica(raiz_ast, stdout);
  if (n_sem > 0 || diag_cuenta(SEM) > 0) {
    codigo = SALIDA_SEM;
    goto fin;
  }

  diag_nota("Análisis completado: sin errores léxicos, sintácticos ni semánticos");

fin:
  if (codigo != SALIDA_OK)
    printf("Resultado: %d error(es) -> %s/errores.txt (código de salida %d)\n",
           diag_total(), dir, codigo);
  nodo_liberar(raiz_ast);
  raiz_ast = NULL;
  lexer_liberar();
  tabla_liberar();
  fclose(f_arbol);
  fclose(f_err);
  fclose(src);
  return codigo;
}
