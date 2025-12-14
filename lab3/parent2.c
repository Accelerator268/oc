#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>

#define SHM_NAME "/child_parent_shm"
#define SHM_SIZE 4096
#define BUFFER_SIZE 1024
#define TIMEOUT_MS 5000  //Таймаут 5 секунд

//Структура для разделяемой памяти
typedef struct {
    char filename[256];
    char result[SHM_SIZE - 256];
    volatile int data_ready;
    volatile int child_done;
} shared_data_t;

//Функция для задержки в миллисекундах
void msleep(int milliseconds) {
    struct timespec ts;
    ts.tv_sec = milliseconds / 1000;
    ts.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

//Очистка ресурсов резделяемой памяти (тип, указатель на разделяемую память и файловый дескриптор)
void cleanup_shm(shared_data_t *shared_data, int shm_fd, bool unlink_shm) {
    if (shared_data != MAP_FAILED) {
        munmap(shared_data, sizeof(shared_data_t));
    }
    if (shm_fd != -1) {
        close(shm_fd);
    }
    if (unlink_shm) {
        shm_unlink(SHM_NAME);
    }
}

//Функция для ожидания данных с таймаутом (нужна, чтоб прога при ошибке на зависала)
int wait_for_data(shared_data_t *shared_data, int timeout_ms) {
    int waited = 0;
    int check_interval = 100;  //Проверяем каждые 100 мс
    
    while (shared_data->data_ready == 0 && waited < timeout_ms) {
        msleep(check_interval);
        waited += check_interval;
        
        
    }
    
    if (shared_data->data_ready == 0) {
        return -1;  //Таймаут
    }
    
    return 0;  //Данные получены
}

int main(int argc, char *argv[]) {
    //Обработка аргументов командной строки
    char *input_file = NULL;
    
    for (int i = 1; i < argc; i++) {
        
        input_file = argv[i];
    }
    
    pid_t pid;
    char filename[256];
    int shm_fd = -1;
    shared_data_t *shared_data = MAP_FAILED;
    bool shm_initialized = false;
    
    //Получаем имя файла
    if (input_file != NULL) {
        strncpy(filename, input_file, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
        
    } else {
        printf("Введите имя файла: ");
        if (fgets(filename, sizeof(filename), stdin) == NULL) {
            fprintf(stderr, "Ошибка чтения имени файла: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        filename[strcspn(filename, "\n")] = '\0';
    }
    
    //Проверяем существование файла
    if (access(filename, F_OK) == -1) {
        fprintf(stderr, "Файл не существует: %s\n", filename);
        exit(EXIT_FAILURE);
    }
    
    //Создаем или открываем разделяемую память
    shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        fprintf(stderr, "Ошибка создания разделяемой памяти: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    
    //Устанавливаем размер разделяемой памяти
    if (ftruncate(shm_fd, sizeof(shared_data_t)) == -1) {
        fprintf(stderr, "Ошибка установки размера разделяемой памяти: %s\n", strerror(errno));
        cleanup_shm(shared_data, shm_fd, true);
        exit(EXIT_FAILURE);
    }
    
    //Отображаем разделяемую память
    shared_data = mmap(NULL, sizeof(shared_data_t), 
                      PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        fprintf(stderr, "Ошибка отображения разделяемой памяти: %s\n", strerror(errno));
        cleanup_shm(shared_data, shm_fd, true);
        exit(EXIT_FAILURE);
    }
    
    shm_initialized = true;
    
    //Инициализируем структуру
    memset(shared_data, 0, sizeof(shared_data_t));
    strncpy(shared_data->filename, filename, sizeof(shared_data->filename) - 1);
    shared_data->data_ready = 0;
    shared_data->child_done = 0;
    
    
    
    //Создаем дочерний процесс
    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "Ошибка создания дочернего процесса: %s\n", strerror(errno));
        cleanup_shm(shared_data, shm_fd, true);
        exit(EXIT_FAILURE);
    }
    
    if (pid == 0) {  //Дочерний процесс
        //Закрываем дескриптор в дочернем процессе
        close(shm_fd);
        
        //Передаем аргументы
        execl("./child", "child", NULL);
        
        //Если execl вернул управление, значит произошла ошибка
        fprintf(stderr, "Ошибка запуска дочерней программы: %s\n", strerror(errno));
        
        //Пытаемся сообщить родителю об ошибке через разделяемую память
        if (shm_initialized) {
            snprintf(shared_data->result, sizeof(shared_data->result),
                    "ОШИБКА: Не удалось запустить дочернюю программу\n");
            shared_data->data_ready = 1;
            shared_data->child_done = 1;
        }
        
        exit(EXIT_FAILURE);
        
    } else {  //Родительский процесс
        printf("Родительский процесс (PID: %d) запустил дочерний (PID: %d)\n", 
               getpid(), pid);
        
        //Сначала ждем завершения дочернего процесса с таймаутом
        int status;
        pid_t waited_pid;
        int wait_timeout = 2000;  //2 секунды на запуск
        
        //Неблокирующее ожидание завершения дочернего процесса
        for (int i = 0; i < wait_timeout / 100; i++) {
            waited_pid = waitpid(pid, &status, WNOHANG);
            
            if (waited_pid == -1) {
                fprintf(stderr, "Ошибка ожидания дочернего процесса: %s\n", strerror(errno));
                cleanup_shm(shared_data, shm_fd, true);
                exit(EXIT_FAILURE);
            } else if (waited_pid == pid) {
                //Дочерний процесс завершился быстро (вероятно, ошибка)
                if (WIFEXITED(status)) {
                    int exit_code = WEXITSTATUS(status);
                    printf("Дочерний процесс завершился быстро с кодом: %d\n", exit_code);
                    
                    if (exit_code != 0) {
                        fprintf(stderr, "Ошибка в дочернем процессе\n");
                        
                        //Проверяем, возможно дочерний процесс успел записать ошибку
                        if (shared_data->data_ready == 1) {
                            printf("Сообщение от дочернего процесса:\n%s", shared_data->result);
                        }
                        
                        cleanup_shm(shared_data, shm_fd, true);
                        exit(EXIT_FAILURE);
                    }
                } else if (WIFSIGNALED(status)) {
                    printf("Дочерний процесс завершился по сигналу: %d\n", WTERMSIG(status));
                    cleanup_shm(shared_data, shm_fd, true);
                    exit(EXIT_FAILURE);
                }
                break;
            }
            
            msleep(100);  //Ждем 100 мс перед следующей проверкой
        }
        
        //Если дочерний процесс все еще работает, ожидаем данных
        waited_pid = waitpid(pid, &status, WNOHANG);
        if (waited_pid == 0) {
            //Дочерний процесс все еще работает, ожидаем данных
            printf("Ожидание данных от дочернего процесса...\n");
            
            int wait_result = wait_for_data(shared_data, TIMEOUT_MS);
            
            if (wait_result == -1) {
                fprintf(stderr, "Таймаут ожидания данных от дочернего процесса\n");
                
                //Прерываем дочерний процесс
                kill(pid, SIGTERM);
                
                //Ждем завершения
                waitpid(pid, &status, 0);
                
                cleanup_shm(shared_data, shm_fd, true);
                exit(EXIT_FAILURE);
            }
            
            //Данные получены
            if (shared_data->data_ready == 1) {
                printf("\n=== Результаты обработки файла '%s' ===\n", filename);
                printf("%s", shared_data->result);
                printf("========================================\n");
                
                //Отмечаем, что данные прочитаны
                shared_data->data_ready = 2;
            }
            
            //Теперь ждем нормального завершения дочернего процесса
            waited_pid = waitpid(pid, &status, 0);
        }
        
        //Обрабатываем завершение дочернего процесса
        if (waited_pid == -1) {
            fprintf(stderr, "Ошибка ожидания дочернего процесса: %s\n", strerror(errno));
        } else if (waited_pid == pid) {
            if (WIFEXITED(status)) {
                int exit_code = WEXITSTATUS(status);
                printf("Дочерний процесс завершился с кодом: %d\n", exit_code);
                
                //Если дочерний процесс завершился с ошибкой
                if (exit_code != 0) {
                    fprintf(stderr, "Внимание: дочерний процесс завершился с ошибкой\n");
                }
            } else if (WIFSIGNALED(status)) {
                printf("Дочерний процесс завершился по сигналу: %d\n", WTERMSIG(status));
            }
        }
        
        //Ожидаем, пока дочерний процесс отметит завершение
        if (shared_data->child_done == 0) {
            //Даем немного времени на установку флага
            for (int i = 0; i < 10 && shared_data->child_done == 0; i++) {
                msleep(100);
            }
        }
        
        
        
        //Очистка
        cleanup_shm(shared_data, shm_fd, true);
        
        printf("Родительский процесс завершен.\n");
    }
    
    return 0;
}