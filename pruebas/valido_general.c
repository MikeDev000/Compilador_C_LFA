#include <stdio.h>

int main() {
    int limite = 5;
    int contador = 0;
    int acumulado = 0;
    float promedio = 0.0;
    char inicial = 'A';
    bool activo = true;

    printf("Ingresa un numero entero:\n");
    scanf("%d", &contador);

    if (contador > 0 && activo) {
        acumulado += contador;
    } else {
        contador = 1;
    }

    while (contador < limite) {
        acumulado += contador;
        contador += 1;
    }

    for (int i = 0; i < 3; i += 1) {
        acumulado += i;
    }

    promedio = acumulado / 2.0;
    printf("Inicial: %c, Acumulado: %d, Promedio: %f\n", inicial, acumulado, promedio);
    return 0;
}