#include "fases.h"
#include "token.h"
#include <stdio.h>
#include <string.h>

static void escapar_dot(FILE *out, const char *s) {
    if (!s) return;
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\n': fputs("\\n", out);  break;
            case '\r': fputs("\\r", out);  break;
            case '\t': fputs("\\t", out);  break;
            default:   fputc(*p, out);     break;
        }
    }
}

static void imprimir_txt_rec(const Nodo *n, FILE *out, int nivel) {
    if (!n) return;

    for (int i = 0; i < nivel; i++) {
        fputs("  ", out);
    }

    const char *nom = n->nombre ? n->nombre : "(sin_nombre)";
    if (n->token) {
        const char *tname = nombre_token(n->token->tipo);
        const char *lex = n->token->lexema ? n->token->lexema : "";
        fprintf(out, "|-- %s [%s: ", nom, tname ? tname : "TOK");
        escapar_dot(out, lex);
        fprintf(out, "] (L%d:C%d)\n", n->linea, n->col);
    } else {
        fprintf(out, "|-- %s (L%d:C%d)\n", nom, n->linea, n->col);
    }

    for (int i = 0; i < n->nhijos; i++) {
        imprimir_txt_rec(n->hijos[i], out, nivel + 1);
    }
}

void arbol_imprimir_txt(Nodo *raiz, FILE *out) {
    if (!out) return;
    if (!raiz) {
        fputs("(Arbol vacio)\n", out);
        return;
    }
    imprimir_txt_rec(raiz, out, 0);
}

static void exportar_dot_rec(const Nodo *n, FILE *out, int *id_gen) {
    if (!n) return;

    int id_actual = (*id_gen)++;
    const char *nom = n->nombre ? n->nombre : "(sin_nombre)";

    if (n->token) {
        const char *tname = nombre_token(n->token->tipo);
        const char *lex = n->token->lexema ? n->token->lexema : "";
        fprintf(out, "  n%d [label=\"", id_actual);
        escapar_dot(out, nom);
        fprintf(out, "\\n[%s] ", tname ? tname : "TOK");
        escapar_dot(out, lex);
        fprintf(out, "\", shape=box, style=filled, fillcolor=\"#E8F4FD\"];\n");
    } else {
        fprintf(out, "  n%d [label=\"", id_actual);
        escapar_dot(out, nom);
        fprintf(out, "\", shape=ellipse, style=filled, fillcolor=\"#F3F4F6\"];\n");
    }

    for (int i = 0; i < n->nhijos; i++) {
        if (!n->hijos[i]) continue;
        int id_hijo = *id_gen;
        exportar_dot_rec(n->hijos[i], out, id_gen);
        fprintf(out, "  n%d -> n%d;\n", id_actual, id_hijo);
    }
}

void arbol_exportar_dot(Nodo *raiz, FILE *out) {
    if (!out) return;
    fputs("digraph ArbolDerivacion {\n", out);
    fputs("  rankdir=TB;\n", out);
    fputs("  node [fontname=\"Arial\", fontsize=10];\n", out);

    if (raiz) {
        int contador_id = 0;
        exportar_dot_rec(raiz, out, &contador_id);
    }

    fputs("}\n", out);
}