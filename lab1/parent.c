#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

#define BUFFER_SIZE 1024

int main() {
    int pipe1[2];
    pid_t pid;
    char filename[256];
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;


    //Получаем имя файла от пользователя
    printf("Введите имя файла: ");
    if (fgets(filename, sizeof(filename), stdin) == NULL) {
        perror("Ошибка чтения имени файла");
        exit(EXIT_FAILURE);
    }


    //Убираем символ новой строки
    filename[strcspn(filename, "\n")] = '\0';


    //Создаем pipe1
    if (pipe(pipe1) == -1) {
        perror("Ошибка создания pipe1");
        exit(EXIT_FAILURE);
    }


    //Создаем дочерний процесс
    pid = fork();
    if (pid == -1) {
        perror("Ошибка создания дочернего процесса");
        exit(EXIT_FAILURE);
    }


    if (pid == 0) {// Дочерний процесс
        close(pipe1[0]);//Закрываем чтение из pipe1
        
        //Перенаправляем стандартный вывод в pipe1[1]
        if (dup2(pipe1[1], STDOUT_FILENO) == -1) {
            perror("Ошибка перенаправления вывода в дочернем процессе");
            exit(EXIT_FAILURE);
        }
        close(pipe1[1]);

        //Запускаем дочернюю программу
        execl("./child", "child", filename, NULL);
        
        //Если execl вернул управление, значит произошла ошибка
        perror("Ошибка запуска дочерней программы");
        exit(EXIT_FAILURE);

    } else {//Родительский процесс
        close(pipe1[1]); //Закрываем запись в pipe1
        
        //Читаем данные из pipe1 и выводим в стандартный вывод
        while ((bytes_read = read(pipe1[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            printf("%s", buffer);
            fflush(stdout);
        }

        if (bytes_read == -1) {
            perror("Ошибка чтения из pipe1");
        }

        close(pipe1[0]);
        
        //Ждем завершения дочернего процесса
        wait(NULL);
        printf("Родительский процесс завершен.\n");
    }

    return 0;
}