#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>


#define MAX_LINE_LENGTH 1024


int main(int argc, char *argv[]) {
    FILE *file;
    char line[MAX_LINE_LENGTH];
    

    //Проверка количества аргументов командной строки
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <имя_файла>\n", argv[0]);
        exit(EXIT_FAILURE);
    }


    //Открываем файл для чтения
    file = fopen(argv[1], "r");
    if (file == NULL) {
        perror("Ошибка открытия файла");
        exit(EXIT_FAILURE);
    }


    //Перенаправляем стандартный ввод на файл
    if (dup2(fileno(file), STDIN_FILENO) == -1) {
        perror("Ошибка перенаправления stdin");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fclose(file);


    //Читаем команды из стандартного ввода (теперь из файла)
    while (fgets(line, sizeof(line), stdin) != NULL) {
        //Убираем символ новой строки
        line[strcspn(line, "\n")] = '\0';


        //Пропускаем пустые строки
        if (strlen(line) == 0) {
            continue;
        }


        //Разбираем строку на числа и считаем
        char *token;
        float sum = 0.0f;
        int count = 0;

        token = strtok(line, " ");
        while (token != NULL) {
            float number;
            if (sscanf(token, "%f", &number) == 1) {
                sum += number;
                count++;
            }
            token = strtok(NULL, " ");
        }


        //Выводим результат
        if (count > 0) {
            printf("Сумма: %.2f (из %d чисел)\n", sum, count);
        } else {
            printf("Не найдено чисел в строке: %s\n", line);
        }
        
        fflush(stdout);//Обеспечиваем немедленный вывод
    }


    if (ferror(stdin)) {
        perror("Ошибка чтения из stdin");
        exit(EXIT_FAILURE);
    }

    return 0;
}