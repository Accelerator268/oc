#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "contracts.h"

int main() {
    printf("Программа 1: статическое связывание\n");
    
    char input[256];
    
    while (1) {
        printf("\nВведите команду: ");
        fgets(input, sizeof(input), stdin);
        
        //Удаляем символ новой строки
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "q") == 0) {
            break;
        }
        
        //Проверяем, какая команда
        if (input[0] == '1') {
            float A, B, e;
            if (sscanf(input + 2, "%f %f %f", &A, &B, &e) == 3) {
                float result = SinIntegral(A, B, e);
                printf("Результат: %f\n", result);
            } else {
                printf("Ошибка: неверные аргументы\n");
            }
        } else if (input[0] == '2') {
            int K;
            if (sscanf(input + 2, "%d", &K) == 1) {
                float result = Pi(K);
                printf("Результат: %f\n", result);
            } else {
                printf("Ошибка: неверные аргументы\n");
            }
        } else {
            printf("Неизвестная команда\n");
        }
    }
    
    return 0;
}