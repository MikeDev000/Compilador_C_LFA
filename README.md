# ccheck — Analizador léxico, sintáctico y semántico para un subconjunto de C

Proyecto Final II · Lenguajes Formales y Autómatas. Una sola ejecución analiza
`entrada.c` en tres fases (léxica → sintáctica → semántica) y genera los reportes.

## Requisitos (versiones probadas)

| Herramienta | Versión probada | Uso |
|---|---|---|
| gcc | 16.2.1 | compilador C (C11) |
| GNU Make | 4.4.1 | build |
| Flex | 2.7.1 | Fase 1 (lexer) |
| GNU Bison | 3.8.2 (mínimo 3.6) | Fase 2 (parser) |
| Graphviz (`dot`) | 16.1.2 | opcional: `arbol.dot` → `arbol.png` |

Instalación: `sudo pacman -S gcc make flex bison graphviz` (Arch) ·
`sudo apt install build-essential flex bison graphviz` (Debian/Ubuntu).

## Compilar

```bash
make            # compila ./ccheck con todas las fases reales (STUBS=0)
make STUBS=1    # opcional: compila usando stubs.c para Dev 3/Dev 4
make clean      # borra binario y archivos generados por Flex/Bison
```

## Uso

```bash
./ccheck entrada.c                 # corre las 3 fases; salidas en salidas/
./ccheck -o otra_carpeta entrada.c # salidas en otra carpeta
./ccheck --tokens entrada.c        # opcional: solo Fase 1 (imprime tabla en pantalla)
./ccheck --tree salida.dot entrada.c # opcional: hasta Fase 2 y exporta a salida.dot
./ccheck --sem entrada.c           # opcional: hasta Fase 3 (igual que por defecto)
make run FILE=pruebas/valido_general.c
make dot                           # salidas/arbol.dot -> salidas/arbol.png
make test                          # corre pruebas/*.c y pruebas/dev1/*.c (salidas/<caso>/)
make test-lexer                    # compara la Fase 1 con los resultados esperados de pruebas/dev1/esperado/
```

### Archivos de salida

| Archivo | Fase | Contenido |
|---|---|---|
| `tokens.txt` | 1 | Tabla de tokens: `TIPO LEXEMA LÍNEA COL` |
| `arbol.txt` | 2 | Árbol de derivación con sangrías (o el motivo por el que no se generó) |
| `arbol.dot` | 2 | Árbol en Graphviz (solo si la fase 2 terminó bien) |
| `errores.txt` | 1–3 | Errores `archivo:línea:col: error[FASE]: mensaje` y notas de fases omitidas |

La interpretación simbólica de `printf`/`scanf` (Fase 3) se imprime en pantalla.

### Corte por fase y códigos de salida

| Código | Significado | Nota que se agrega a `errores.txt` |
|---|---|---|
| 0 | Sin errores | `Análisis completado: sin errores ...` |
| 1 | Errores léxicos | `Fases sintáctica y semántica omitidas por errores léxicos` |
| 2 | Error sintáctico | `Fase semántica omitida por errores sintácticos` |
| 3 | Errores semánticos | — |
| 4 | Error de uso o de E/S | (archivo inexistente, argumentos inválidos) |

- El **lexer** reporta *todos* sus errores y sigue leyendo.
- El **parser** se detiene en el *primer* error (sin modo pánico).
- El **semántico** reporta todos los que encuentre.

## Estructura

```
Makefile, README.md            Dev 1
token.h token.c diag.h diag.c  Dev 1  (interfaces + implementación)
lexer.l                        Dev 1  (Fase 1)
main.c fases.h                 Dev 1  (driver + contratos con Dev 2/3/4)
stubs.c                        Dev 1  (provisional opcional: make STUBS=1)
parser.y ast.h                 Dev 2  (Fase 2: GLC LALR(1) sin conflictos)
semantico.c interp.c types.h symtab.h symtab.c   Dev 3  (Fase 3)
arbol.c                        Dev 4  (árbol a texto/Graphviz)
pruebas/                       casos de prueba (pruebas/dev1/: casos del lexer)
salidas/                       reportes generados
docs/                          workpath, interfaces, secciones del informe
```

Documentación para el equipo:
- [docs/INTERFACES.md](docs/INTERFACES.md): contratos entre módulos (tokens, errores, árbol, funciones que llama `main.c`) y pasos de integración para Dev 2, 3 y 4.
- [docs/dev1_seccion_lexica.md](docs/dev1_seccion_lexica.md): sección de la fase léxica para el informe.
