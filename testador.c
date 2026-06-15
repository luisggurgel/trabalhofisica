#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

// Função segura para exponenciação (a ^ b) com detecção de overflow
uint64_t safe_pow(uint64_t base, uint64_t exp, bool *overflow) {
    if (*overflow) return 0;
    if (exp == 0) return 1;
    if (base == 0) return 0;
    if (base == 1) return 1;

    uint64_t result = 1;
    for (uint64_t i = 0; i < exp; i++) {
        // Verifica se a próxima multiplicação vai estourar o limite de 64 bits
        // Se base * result > UINT64_MAX, então UINT64_MAX / base < result
        if (UINT64_MAX / base < result) {
            *overflow = true;
            return 0;
        }
        result *= base;
    }
    return result;
}

// Função recursiva para a notação de Knuth
uint64_t knuth_arrow(uint64_t base, uint64_t arrows, uint64_t b, bool *overflow) {
    // Se o overflow já foi detectado em chamadas profundas, aborta a execução
    if (*overflow) return 0;

    // Caso base 1: Uma seta é exponenciação padrão
    if (arrows == 1) {
        return safe_pow(base, b, overflow);
    }

    // Caso base 2: Se o operador à direita for 0, o resultado é 1
    // Isso vale para a ^ n 0 = 1, para n >= 1
    if (b == 0) {
        return 1;
    }

    // Passo recursivo de Knuth: a ↑^n b = a ↑^(n-1) (a ↑^n (b-1))
    uint64_t inner_result = knuth_arrow(base, arrows, b - 1, overflow);
    
    if (*overflow) return 0; // Aborta antes da próxima chamada se o limite estourou
    
    return knuth_arrow(base, arrows - 1, inner_result, overflow);
}

int main() {
    uint64_t base, arrows, b;
    bool overflow;

    printf("--- Calculadora de Setas de Knuth ---\n");
    printf("Formato: base setas b (ex: 3 2 3 para 3 ^^ 3)\n");
    
    // Casos de teste automatizados
    uint64_t tests[][3] = {
        {3, 1, 4},   // 3 ^ 4 = 81
        {3, 2, 3},   // 3 ^^ 3 = 3^(3^3) = 3^27 = 7625597484987
        {3, 2, 4},   // 3 ^^ 4 = 3^(7625597484987) -> Vai estourar!
        {2, 3, 3}    // 2 ^^^ 3 = 2 ^^ (2 ^^ 2) = 2 ^^ 4 = 65536
    };

    int num_tests = sizeof(tests) / sizeof(tests[0]);

    for(int i = 0; i < num_tests; i++) {
        base = tests[i][0];
        arrows = tests[i][1];
        b = tests[i][2];
        overflow = false;

        printf("\nCalculando: %llu com %llu seta(s) para %llu\n", base, arrows, b);
        
        uint64_t result = knuth_arrow(base, arrows, b, &overflow);

        if (overflow) {
            printf("[!] Resultado: OVERFLOW DETECTADO. A operacao ultrapassa %llu.\n", UINT64_MAX);
        } else {
            printf("[+] Resultado: %llu\n", result);
        }
    }

    return 0;
}
