#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#define SHM_NAME "/child_parent_shm"
#define MAX_LINE_LENGTH 1024
#define RESULT_LINE_SIZE 512  //Увеличен размер буфера для результата

//Структура для разделяемой памяти
typedef struct {
    char filename[256];
    char result[4096 - 256];
    volatile int data_ready;
    volatile int child_done;
} shared_data_t;

//Очистка ресурсов резделяемой памяти (тип, указатель на разделяемую память и файловый дескриптор)
void cleanup_shm(shared_data_t *shared_data, int shm_fd) {
    if (shared_data != MAP_FAILED) {
        munmap(shared_data, sizeof(shared_data_t));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
}

//Более точная задержка через nanosleep (там еще были варианты sleep и usleep)
void msleep(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

//Функция для подсчета суммы чисел в строке без модификации строки
float process_line(const char *line, int *count) {
    float sum = 0.0f;
    *count = 0;
    
    const char *ptr = line;
    while (*ptr) {
        //Пропускаем пробелы
        while (*ptr == ' ') ptr++;
        
        if (*ptr == '\0') break;
        
        //Пытаемся прочитать число
        float number;
        int chars_read;
        if (sscanf(ptr, "%f%n", &number, &chars_read) == 1) {
            sum += number;
            (*count)++;
            ptr += chars_read;
        } else {
            //Пропускаем нечисловые символы
            ptr++;
        }
    }
    
    return sum;
}

//Функция для безопасного форматирования строки с ограничением длины
void format_result_line(char *buffer, size_t buffer_size, 
                       const char *original_line, float sum, int count, int has_numbers) {
    //Ограничиваем длину оригинальной строки для вывода
    char limited_line[256];
    size_t orig_len = strlen(original_line);
    
    if (orig_len > 200) {  //Ограничиваем до 200 символов для вывода
        strncpy(limited_line, original_line, 197);
        limited_line[197] = '\0';
        strcat(limited_line, "...");
    } else {
        strncpy(limited_line, original_line, sizeof(limited_line) - 1);
        limited_line[sizeof(limited_line) - 1] = '\0';
    }
    
    if (has_numbers) {
        snprintf(buffer, buffer_size, 
                "Сумма: %.2f (из %d чисел) для строки: %s\n", 
                sum, count, limited_line);
    } else {
        snprintf(buffer, buffer_size, 
                "Не найдено чисел в строке: %s\n", limited_line);
    }
}

int main(int argc, char *argv[]) {
    //Проверяем аргументы командной строки для отладочного режима
    int debug_mode = 0;
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-d") == 0) {
                debug_mode = 1;
            }
        }
    }
    
    if (debug_mode) {
        printf("Дочерний процесс запущен в режиме отладки\n");
    }
    
    FILE *file;
    char line[MAX_LINE_LENGTH];
    char original_line[MAX_LINE_LENGTH];
    int shm_fd = -1;
    shared_data_t *shared_data = MAP_FAILED;
    char output_buffer[4096] = {0};
    int output_pos = 0;
    char result_line[RESULT_LINE_SIZE];
    
    //Открываем существующую разделяемую память
    shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        fprintf(stderr, "Ошибка открытия разделяемой памяти: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    //Отображаем разделяемую память
    shared_data = mmap(NULL, sizeof(shared_data_t), 
                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        fprintf(stderr, "Ошибка отображения разделяемой памяти: %s\n", strerror(errno));
        cleanup_shm(shared_data, shm_fd);
        exit(EXIT_FAILURE);
    }
    
    //Открываем файл для чтения
    file = fopen(shared_data->filename, "r");
    if (file == NULL) {
        format_result_line(output_buffer, sizeof(output_buffer),
                          shared_data->filename, 0, 0, 0);
        //Перезаписываем начало буфера с сообщением об ошибке
        snprintf(output_buffer, sizeof(output_buffer),
                "Ошибка открытия файла '%s': %s\n", 
                shared_data->filename, strerror(errno));
        goto send_results;
    }
    
    if (debug_mode) {
        printf("Открыт файл: %s\n", shared_data->filename);
    }
    
    //Читаем команды из файла
    while (fgets(line, sizeof(line), file) != NULL) {
        //Убираем символ новой строки
        line[strcspn(line, "\n")] = '\0';
        
        //Сохраняем оригинальную строку
        strncpy(original_line, line, sizeof(original_line) - 1);
        original_line[sizeof(original_line) - 1] = '\0';
        
        //Пропускаем пустые строки
        if (strlen(line) == 0) {
            continue;
        }
        
        //Подсчитываем сумму чисел в строке
        int count = 0;
        float sum = process_line(line, &count);
        
        //Формируем результат с ограничением длины
        format_result_line(result_line, sizeof(result_line),
                          original_line, sum, count, count > 0);
        
        //Добавляем в буфер вывода
        size_t result_len = strlen(result_line);
        if (output_pos + result_len < sizeof(output_buffer) - 1) {
            strcpy(output_buffer + output_pos, result_line);
            output_pos += result_len;
        } else {
            //Если буфер переполняется, добавляем сообщение и выходим
            const char *trunc_msg = "\n... (вывод обрезан, слишком много данных)\n";
            size_t trunc_msg_len = strlen(trunc_msg);
            
            //Оставляем место для сообщения об обрезке
            if (output_pos + trunc_msg_len < sizeof(output_buffer)) {
                strcpy(output_buffer + output_pos, trunc_msg);
            }
            break;
        }
        
        // Небольшая задержка для демонстрации (только в debug режиме)
        if (debug_mode) {
            msleep(100);
            printf("Обработана строка (первые 100 символов): %.100s\n", original_line);
        }
    }
    
    if (ferror(file)) {
        //Добавляем сообщение об ошибке в конец вывода
        char error_msg[256];
        snprintf(error_msg, sizeof(error_msg),
                "Ошибка чтения файла: %s\n", strerror(errno));
        
        size_t error_len = strlen(error_msg);
        if (output_pos + error_len < sizeof(output_buffer) - 1) {
            strcpy(output_buffer + output_pos, error_msg);
        }
    }
    
    fclose(file);
    
send_results:
    //Копируем результаты в разделяемую память
    strncpy(shared_data->result, output_buffer, sizeof(shared_data->result) - 1);
    shared_data->result[sizeof(shared_data->result) - 1] = '\0';
    
    if (debug_mode) {
        printf("Данные подготовлены, ожидание родителя...\n");
        printf("Размер данных: %zu байт\n", strlen(output_buffer));
    }
    
    //Отмечаем, что данные готовы
    shared_data->data_ready = 1;
    
    //Ждем, пока родитель прочитает данные
    while (shared_data->data_ready == 1) {
        msleep(50);
    }
    
    //Отмечаем завершение работы
    shared_data->child_done = 1;
    
    if (debug_mode) {
        printf("Дочерний процесс завершен\n");
    }
    
    //Очистка
    cleanup_shm(shared_data, shm_fd);
    
    return 0;
}