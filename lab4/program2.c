#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

// Определяем типы функций
typedef float (*SinIntegralFunc)(float, float, float);
typedef float (*PiFunc)(int);

typedef struct {
    void* handle;
    SinIntegralFunc sin_integral;
    PiFunc pi;
    const char* name;
} Library;

int main() {
    printf("Программа 2: динамическая загрузка библиотек\n");
    
    // Инициализация библиотек
    Library libs[2] = {
        {NULL, NULL, NULL, "Прямоугольники/Лейбниц"},
        {NULL, NULL, NULL, "Трапеции/Валлис"}
    };
    
    const char* lib_paths[2] = {
        "./libimplementation1.so",
        "./libimplementation2.so"
    };
    
    // Загружаем первую библиотеку по умолчанию
    libs[0].handle = dlopen(lib_paths[0], RTLD_LAZY);
    if (!libs[0].handle) {
        fprintf(stderr, "Ошибка загрузки библиотеки 1: %s\n", dlerror());
        return 1;
    }
    
    // Получаем указатели на функции из первой библиотеки
    libs[0].sin_integral = (SinIntegralFunc)dlsym(libs[0].handle, "SinIntegral");
    libs[0].pi = (PiFunc)dlsym(libs[0].handle, "Pi");
    
    if (!libs[0].sin_integral || !libs[0].pi) {
        fprintf(stderr, "Ошибка получения функций из библиотеки 1: %s\n", dlerror());
        dlclose(libs[0].handle);
        return 1;
    }
    
    int current_lib = 0;  //Текущая активная библиотека
    
    printf("Текущая реализация: %s\n", libs[current_lib].name);
    
    char input[256];
    
    while (1) {
        printf("\nВведите команду: ");
        fgets(input, sizeof(input), stdin);
        
        //Удаляем символ новой строки
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "q") == 0) {
            break;
        }
        
        // Переключение библиотек
        if (strcmp(input, "0") == 0) {
            //Закрываем текущую библиотеку
            dlclose(libs[current_lib].handle);
            
            //Переключаемся на другую библиотеку
            current_lib = (current_lib + 1) % 2;
            
            //Если вторая библиотека еще не загружена, загружаем ее
            if (!libs[current_lib].handle) {
                libs[current_lib].handle = dlopen(lib_paths[current_lib], RTLD_LAZY);
                if (!libs[current_lib].handle) {
                    fprintf(stderr, "Ошибка загрузки библиотеки: %s\n", dlerror());
                    //Возвращаемся к предыдущей библиотеке
                    current_lib = (current_lib + 1) % 2;
                    continue;
                }
                
                libs[current_lib].sin_integral = (SinIntegralFunc)dlsym(libs[current_lib].handle, "SinIntegral");
                libs[current_lib].pi = (PiFunc)dlsym(libs[current_lib].handle, "Pi");
                
                if (!libs[current_lib].sin_integral || !libs[current_lib].pi) {
                    fprintf(stderr, "Ошибка получения функций: %s\n", dlerror());
                    dlclose(libs[current_lib].handle);
                    libs[current_lib].handle = NULL;
                    //Возвращаемся к предыдущей библиотеке
                    current_lib = (current_lib + 1) % 2;
                    continue;
                }
            }
            
            printf("Переключено на реализацию: %s\n", libs[current_lib].name);
        }
        //Вычисление интеграла
        else if (input[0] == '1') {
            float A, B, e;
            if (sscanf(input + 2, "%f %f %f", &A, &B, &e) == 3) {
                float result = libs[current_lib].sin_integral(A, B, e);
                printf("Результат (%s): %f\n", libs[current_lib].name, result);
            } else {
                printf("Ошибка: неверные аргументы\n");
            }
        }
        //Вычисление Pi
        else if (input[0] == '2') {
            int K;
            if (sscanf(input + 2, "%d", &K) == 1) {
                float result = libs[current_lib].pi(K);
                printf("Результат (%s): %f\n", libs[current_lib].name, result);
            } else {
                printf("Ошибка: неверные аргументы\n");
            }
        }
        else {
            printf("Неизвестная команда\n");
        }
    }
    
    //Закрываем все загруженные библиотеки
    for (int i = 0; i < 2; i++) {
        if (libs[i].handle) {
            dlclose(libs[i].handle);
        }
    }
    
    return 0;
}