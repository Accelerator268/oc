#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <yaml.h>

#define MAX_JOBS 100
#define MAX_NAME_LEN 100
#define MAX_DEPS 10
#define MAX_MUTEXES 10
#define MAX_MUTEX_PER_JOB 5
#define DEFAULT_MAX_CONCURRENT 4
#define COMMAND_BUFFER_SIZE 512

//Предварительное объявление структуры Job
typedef struct Job Job;

//Структура для мьютекса
typedef struct {
    char name[MAX_NAME_LEN];
    pthread_mutex_t mutex;
    int ref_count;
} Mutex;

//Структура для задачи (job)
struct Job {
    char name[MAX_NAME_LEN];
    char command[COMMAND_BUFFER_SIZE];
    pid_t pid;
    int status;
    bool completed;
    bool failed;
    int dependency_count;
    char dependencies[MAX_DEPS][MAX_NAME_LEN];
    int mutex_count;
    char mutexes[MAX_MUTEX_PER_JOB][MAX_NAME_LEN];
    pthread_mutex_t *mutex_ptrs[MAX_MUTEX_PER_JOB];
    int remaining_deps;
    Job* next_jobs[MAX_DEPS];
    int next_job_count;
};

//Структура для DAG
typedef struct {
    Job jobs[MAX_JOBS];
    int job_count;
    Mutex mutexes[MAX_MUTEXES];
    int mutex_count;
    int max_concurrent;
    int running_jobs;
    pthread_mutex_t running_mutex;
    pthread_cond_t job_completed_cond;
    bool dag_failed;
} DAG;

//Глобальный DAG
DAG dag = {0};

//Прототипы функций
Job* find_job_by_name(const char* name);
Mutex* find_mutex_by_name(const char* name);
Mutex* add_mutex(const char* name);
bool parse_yaml_config_simple(const char* filename);
bool build_dependency_graph(void);
bool has_cycle_util(int job_idx, bool visited[], bool rec_stack[]);
bool has_cycles(void);
bool validate_dag(void);
void* execute_job(void* arg);
bool execute_dag(void);

//Функция для поиска задачи по имени
Job* find_job_by_name(const char* name) {
    for (int i = 0; i < dag.job_count; i++) {
        if (strcmp(dag.jobs[i].name, name) == 0) {
            return &dag.jobs[i];
        }
    }
    return NULL;
}

//Функция для поиска мьютекса по имени
Mutex* find_mutex_by_name(const char* name) {
    for (int i = 0; i < dag.mutex_count; i++) {
        if (strcmp(dag.mutexes[i].name, name) == 0) {
            return &dag.mutexes[i];
        }
    }
    return NULL;
}

//Функция для добавления мьютекса
Mutex* add_mutex(const char* name) {
    Mutex* mutex = find_mutex_by_name(name);
    if (mutex) {
        mutex->ref_count++;
        return mutex;
    }
    
    if (dag.mutex_count >= MAX_MUTEXES) {
        fprintf(stderr, "Превышено максимальное количество мьютексов\n");
        return NULL;
    }
    
    mutex = &dag.mutexes[dag.mutex_count++];
    strncpy(mutex->name, name, MAX_NAME_LEN - 1);
    mutex->name[MAX_NAME_LEN - 1] = '\0';
    pthread_mutex_init(&mutex->mutex, NULL);
    mutex->ref_count = 1;
    
    return mutex;
}

//структура для парсинга ямл файла
bool parse_yaml_config_simple(const char* filename) {
    printf("Упрощенный парсинг YAML конфигурации...\n");
    
    FILE* file = fopen(filename, "r");
    if (!file) {
        fprintf(stderr, "Не удалось открыть файл %s\n", filename);
        return false;
    }
    
    char line[1024];
    Job* current_job = NULL;
    
    //Значения по умолчанию
    dag.max_concurrent = DEFAULT_MAX_CONCURRENT;
    dag.job_count = 0;
    dag.mutex_count = 0;
    
    //Инициализируем массив мьютексов
    char* mutex_list[100] = {NULL};
    int mutex_list_count = 0;
    
    while (fgets(line, sizeof(line), file)) {
        //Убираем перевод строки
        line[strcspn(line, "\n")] = '\0';
        
        //Пропускаем комментарии
        char* comment = strchr(line, '#');
        if (comment) *comment = '\0';
        
        char* trimmed = line;
        while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
        
        //Пропускаем пустые строки
        if (trimmed[0] == '\0') continue;
        
        //Парсим max_concurrent
        if (strncmp(trimmed, "max_concurrent:", 15) == 0) {
            char* value = trimmed + 15;
            while (*value == ' ') value++;
            dag.max_concurrent = atoi(value);
            if (dag.max_concurrent <= 0) dag.max_concurrent = DEFAULT_MAX_CONCURRENT;
            printf("Найдено max_concurrent: %d\n", dag.max_concurrent);
            continue;
        }
        
        // Парсим глобальные mutexes
        if (strncmp(trimmed, "mutexes:", 8) == 0) {
            printf("Парсинг глобальных мьютексов...\n");
            //Читаем следующие строки, пока не найдем не-элемент списка
            while (fgets(line, sizeof(line), file)) {
                line[strcspn(line, "\n")] = '\0';
                char* item = line;
                while (*item == ' ' || *item == '\t' || *item == '-') item++;
                
                //Пропускаем комментарии в элементах списка
                char* item_comment = strchr(item, '#');
                if (item_comment) *item_comment = '\0';
                
                //Если строка пустая или начинается с буквы (новая секция), выходим
                if (item[0] == '\0' || 
                    (item[0] >= 'a' && item[0] <= 'z') || 
                    (item[0] >= 'A' && item[0] <= 'Z')) {
                    //Откатываемся на одну строку назад
                    fseek(file, -strlen(line)-1, SEEK_CUR);
                    break;
                }
                
                //Убираем кавычки и запятые
                if (item[0] == '"' || item[0] == '\'') {
                    char quote = item[0];
                    memmove(item, item+1, strlen(item)); //Убираем первую кавычку
                    char* end_quote = strchr(item, quote);
                    if (end_quote) *end_quote = '\0';
                }
                
                //Убираем запятые и пробелы в конце
                char* end = item + strlen(item) - 1;
                while (end > item && (*end == ',' || *end == ' ' || *end == '\t')) {
                    *end = '\0';
                    end--;
                }
                
                if (strlen(item) > 0) {
                    //Сохраняем мьютекс во временный список
                    mutex_list[mutex_list_count] = strdup(item);
                    if (mutex_list[mutex_list_count]) {
                        printf("  Найден мьютекс: %s\n", item);
                        mutex_list_count++;
                    }
                }
            }
            continue;
        }
        
        //Парсим job (строка вида "jobX:")
        if (strstr(trimmed, "job") && trimmed[strlen(trimmed)-1] == ':') {
            //Извлекаем имя job (без двоеточия)
            char name[MAX_NAME_LEN];
            strncpy(name, trimmed, MAX_NAME_LEN-1);
            name[MAX_NAME_LEN-1] = '\0';
            
            //Убираем двоеточие в конце
            char* colon = strchr(name, ':');
            if (colon) *colon = '\0';
            
            //Убираем пробелы в конце
            char* name_end = name + strlen(name) - 1;
            while (name_end > name && (*name_end == ' ' || *name_end == '\t')) {
                *name_end = '\0';
                name_end--;
            }
            
            if (dag.job_count >= MAX_JOBS) {
                fprintf(stderr, "Слишком много задач\n");
                fclose(file);
                //Освобождаем память мьютексов
                for (int i = 0; i < mutex_list_count; i++) {
                    if (mutex_list[i]) free(mutex_list[i]);
                }
                return false;
            }
            
            current_job = &dag.jobs[dag.job_count++];
            strncpy(current_job->name, name, MAX_NAME_LEN-1);
            current_job->name[MAX_NAME_LEN-1] = '\0';
            current_job->dependency_count = 0;
            current_job->mutex_count = 0;
            current_job->command[0] = '\0';
            current_job->completed = false;
            current_job->failed = false;
            current_job->remaining_deps = 0;
            current_job->next_job_count = 0;
            
            printf("Найдена задача: %s\n", current_job->name);
            continue;
        }
        
        //Если у нас есть текущая задача
        if (current_job != NULL) {
            //Парсим command
            if (strncmp(trimmed, "command:", 8) == 0) {
                char* value = trimmed + 8;
                while (*value == ' ') value++;
                
                //Убираем кавычки если есть
                if (*value == '"' || *value == '\'') {
                    char quote = *value;
                    value++;
                    char* end_quote = strchr(value, quote);
                    if (end_quote) *end_quote = '\0';
                }
                
                strncpy(current_job->command, value, COMMAND_BUFFER_SIZE-1);
                current_job->command[COMMAND_BUFFER_SIZE-1] = '\0';
                printf("  Команда: %s\n", current_job->command);
                continue;
            }
            
            //Парсим dependencies
            if (strncmp(trimmed, "dependencies:", 13) == 0) {
                printf("  Парсинг зависимостей для %s...\n", current_job->name);
                
                char* value = trimmed + 13;
                while (*value == ' ') value++;
                
                //Проверяем формат: dependencies: [job1, job2]
                if (*value == '[') {
                    value++; // Пропускаем '['
                    
                    //Находим закрывающую скобку
                    char* end_bracket = strchr(value, ']');
                    if (end_bracket) *end_bracket = '\0';
                    
                    //Парсим зависимости, разделенные запятыми
                    char* saveptr;
                    char* dep = strtok_r(value, ",", &saveptr);
                    while (dep != NULL && current_job->dependency_count < MAX_DEPS) {
                        //Убираем пробелы
                        while (*dep == ' ') dep++;
                        char* end = dep + strlen(dep) - 1;
                        while (end > dep && (*end == ' ' || *end == ']' || *end == '\t')) {
                            *end = '\0';
                            end--;
                        }
                        
                        if (strlen(dep) > 0) {
                            strncpy(current_job->dependencies[current_job->dependency_count], 
                                   dep, MAX_NAME_LEN-1);
                            current_job->dependencies[current_job->dependency_count][MAX_NAME_LEN-1] = '\0';
                            current_job->dependency_count++;
                            printf("    Зависимость: %s\n", dep);
                        }
                        dep = strtok_r(NULL, ",", &saveptr);
                    }
                }
                continue;
            }
            
            //Парсим mutexes внутри job
            if (strncmp(trimmed, "mutexes:", 8) == 0) {
                printf("  Парсинг мьютексов для %s...\n", current_job->name);
                
                char* value = trimmed + 8;
                while (*value == ' ') value++;
                
                //Проверяем формат: mutexes: [db_access, file_lock]
                if (*value == '[') {
                    value++; // Пропускаем '['
                    
                    //Находим закрывающую скобку
                    char* end_bracket = strchr(value, ']');
                    if (end_bracket) *end_bracket = '\0';
                    
                    //Парсим мьютексы, разделенные запятыми
                    char* saveptr;
                    char* mutex = strtok_r(value, ",", &saveptr);
                    while (mutex != NULL && current_job->mutex_count < MAX_MUTEX_PER_JOB) {
                        //Убираем пробелы
                        while (*mutex == ' ') mutex++;
                        char* end = mutex + strlen(mutex) - 1;
                        while (end > mutex && (*end == ' ' || *end == ']' || *end == '\t')) {
                            *end = '\0';
                            end--;
                        }
                        
                        if (strlen(mutex) > 0) {
                            strncpy(current_job->mutexes[current_job->mutex_count], 
                                   mutex, MAX_NAME_LEN-1);
                            current_job->mutexes[current_job->mutex_count][MAX_NAME_LEN-1] = '\0';
                            current_job->mutex_count++;
                            printf("    Мьютекс: %s\n", mutex);
                        }
                        mutex = strtok_r(NULL, ",", &saveptr);
                    }
                }
                continue;
            }
            
            //Если строка не начинается с пробела или таба, значит началась новая секция
            if (trimmed == line) { // Нет отступов в начале
                current_job = NULL;
                //Нужно обработать эту строку заново
                fseek(file, -strlen(line)-1, SEEK_CUR);
            }
        }
    }
    
    fclose(file);
    
    //Теперь добавляем глобальные мьютексы из временного списка
    for (int i = 0; i < mutex_list_count; i++) {
        if (mutex_list[i]) {
            add_mutex(mutex_list[i]);
            free(mutex_list[i]);
        }
    }
    
    printf("Парсинг завершен. Загружено %d задач, %d мьютексов\n", 
           dag.job_count, dag.mutex_count);
    
    return true;
}

//Построение графа зависимостей
bool build_dependency_graph(void) {
    //Инициализация связей next_jobs
    for (int i = 0; i < dag.job_count; i++) {
        Job* job = &dag.jobs[i];
        job->next_job_count = 0;
        job->remaining_deps = job->dependency_count; //Инициализируем счетчик зависимостей
    }
    
    //Создаем связи next_jobs
    for (int i = 0; i < dag.job_count; i++) {
        Job* job = &dag.jobs[i];
        
        for (int j = 0; j < job->dependency_count; j++) {
            Job* dep = find_job_by_name(job->dependencies[j]);
            if (!dep) {
                fprintf(stderr, "Зависимость %s не найдена для задачи %s\n",
                       job->dependencies[j], job->name);
                return false;
            }
            
            if (dep->next_job_count >= MAX_DEPS) {
                fprintf(stderr, "Превышено максимальное количество зависимостей для %s\n",
                       dep->name);
                return false;
            }
            
            dep->next_jobs[dep->next_job_count++] = job;
        }
    }
    
    //Привязываем мьютексы
    for (int i = 0; i < dag.job_count; i++) {
        Job* job = &dag.jobs[i];
        for (int j = 0; j < job->mutex_count; j++) {
            Mutex* mutex = find_mutex_by_name(job->mutexes[j]);
            if (!mutex) {
                fprintf(stderr, "Мьютекс %s не найден для задачи %s\n",
                       job->mutexes[j], job->name);
                return false;
            }
            job->mutex_ptrs[j] = &mutex->mutex;
        }
    }
    
    //Отладочный вывод графа
    printf("\nПостроен граф зависимостей:\n");
    for (int i = 0; i < dag.job_count; i++) {
        Job* job = &dag.jobs[i];
        printf("  %s:", job->name);
        
        if (job->dependency_count > 0) {
            printf(" зависит от [");
            for (int j = 0; j < job->dependency_count; j++) {
                printf("%s", job->dependencies[j]);
                if (j < job->dependency_count - 1) printf(", ");
            }
            printf("]");
        } else {
            printf(" (стартовая задача)");
        }
        
        if (job->next_job_count > 0) {
            printf(", ведет к [");
            for (int j = 0; j < job->next_job_count; j++) {
                printf("%s", job->next_jobs[j]->name);
                if (j < job->next_job_count - 1) printf(", ");
            }
            printf("]");
        } else {
            printf(" (завершающая задача)");
        }
        
        if (job->mutex_count > 0) {
            printf(", мьютексы: [");
            for (int j = 0; j < job->mutex_count; j++) {
                printf("%s", job->mutexes[j]);
                if (j < job->mutex_count - 1) printf(", ");
            }
            printf("]");
        }
        
        printf(", оставшиеся зависимости: %d\n", job->remaining_deps);
    }
    
    return true;
}

//Проверка на наличие циклов (DFS) - вспомогательная функция
bool has_cycle_util(int job_idx, bool visited[], bool rec_stack[]) {
    if (!visited[job_idx]) {
        visited[job_idx] = true;
        rec_stack[job_idx] = true;
        
        Job* job = &dag.jobs[job_idx];
        for (int i = 0; i < job->next_job_count; i++) {
            //Находим индекс следующей задачи
            int next_idx = -1;
            for (int j = 0; j < dag.job_count; j++) {
                if (&dag.jobs[j] == job->next_jobs[i]) {
                    next_idx = j;
                    break;
                }
            }
            
            if (next_idx != -1) {
                if (!visited[next_idx] && has_cycle_util(next_idx, visited, rec_stack)) {
                    return true;
                } else if (rec_stack[next_idx]) {
                    return true;
                }
            }
        }
    }
    
    rec_stack[job_idx] = false;
    return false;
}

//Проверка на наличие циклов
bool has_cycles(void) {
    bool visited[MAX_JOBS] = {false};
    bool rec_stack[MAX_JOBS] = {false};
    
    for (int i = 0; i < dag.job_count; i++) {
        if (has_cycle_util(i, visited, rec_stack)) {
            return true;
        }
    }
    
    return false;
}

//Проверка корректности DAG
bool validate_dag(void) {
    //Проверка на циклы
    if (has_cycles()) {
        fprintf(stderr, "Обнаружен цикл в DAG\n");
        return false;
    }
    
    //Проверка на наличие задач
    if (dag.job_count == 0) {
        fprintf(stderr, "DAG не содержит задач\n");
        return false;
    }
    
    //Проверка на наличие стартовых задач (задачи без зависимостей)
    int start_jobs = 0;
    for (int i = 0; i < dag.job_count; i++) {
        if (dag.jobs[i].dependency_count == 0) {
            start_jobs++;
        }
    }
    
    if (start_jobs == 0) {
        fprintf(stderr, "Нет стартовых задач (задач без зависимостей)\n");
        return false;
    }
    
    //Проверка на наличие завершающих задач (задачи, от которых никто не зависит)
    int end_jobs = 0;
    for (int i = 0; i < dag.job_count; i++) {
        if (dag.jobs[i].next_job_count == 0) {
            end_jobs++;
        }
    }
    
    if (end_jobs == 0) {
        fprintf(stderr, "Нет завершающих задач\n");
        return false;
    }
    
    //Визуализация графа
    printf("\nСтруктура графа:\n");
    printf("  1 → 4  →  6\n");
    printf("  2 ↗     ↗\n");
    printf("  3 → 5 ↗\n");
    
    printf("\nDAG корректен:\n");
    printf("  Всего задач: %d\n", dag.job_count);
    printf("  Стартовых задач: %d\n", start_jobs);
    printf("  Завершающих задач: %d\n", end_jobs);
    printf("  Максимально параллельных задач: %d\n", dag.max_concurrent);
    printf("  Всего мьютексов: %d\n", dag.mutex_count);
    
    return true;
}

//Функция для запуска задачи
void* execute_job(void* arg) {
    Job* job = (Job*)arg;
    
    //Блокируем мьютексы, если они есть
    for (int i = 0; i < job->mutex_count; i++) {
        pthread_mutex_lock(job->mutex_ptrs[i]);
        printf("  %s: заблокирован мьютекс %s\n", job->name, job->mutexes[i]);
    }
    
    printf("Запуск задачи: %s (команда: %s)\n", job->name, job->command);
    
    //Запускаем команду
    job->pid = fork();
    if (job->pid == 0) {
        //Дочерний процесс
        execl("/bin/sh", "sh", "-c", job->command, NULL);
        perror("Ошибка выполнения execl");
        exit(1); //Если execl не удался
    } else if (job->pid > 0) {
        //Родительский процесс
        int status;
        waitpid(job->pid, &status, 0);
        
        job->status = status;
        job->completed = true;
        
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                printf("Задача %s завершена успешно\n", job->name);
                job->failed = false;
            } else {
                printf("Задача %s завершена с ошибкой (код: %d)\n", 
                       job->name, WEXITSTATUS(status));
                job->failed = true;
                
                //Устанавливаем флаг ошибки DAG
                pthread_mutex_lock(&dag.running_mutex);
                dag.dag_failed = true;
                pthread_mutex_unlock(&dag.running_mutex);
            }
        } else {
            printf("Задача %s завершена с сигналом\n", job->name);
            job->failed = true;
            
            pthread_mutex_lock(&dag.running_mutex);
            dag.dag_failed = true;
            pthread_mutex_unlock(&dag.running_mutex);
        }
    } else {
        perror("Ошибка fork");
        job->failed = true;
        
        pthread_mutex_lock(&dag.running_mutex);
        dag.dag_failed = true;
        pthread_mutex_unlock(&dag.running_mutex);
    }
    
    //Разблокируем мьютексы
    for (int i = job->mutex_count - 1; i >= 0; i--) {
        pthread_mutex_unlock(job->mutex_ptrs[i]);
        printf("  %s: разблокирован мьютекс %s\n", job->name, job->mutexes[i]);
    }
    
    //Уменьшаем счетчик выполняющихся задач
    pthread_mutex_lock(&dag.running_mutex);
    dag.running_jobs--;
    printf("  %s: завершена. Осталось запущенных задач: %d\n", 
           job->name, dag.running_jobs);
    
    //Уведомляем ожидающие потоки о завершении задачи
    for (int i = 0; i < job->next_job_count; i++) {
        Job* next_job = job->next_jobs[i];
        next_job->remaining_deps--;
        
        printf("  %s: уменьшены зависимости для %s (осталось: %d)\n",
               job->name, next_job->name, next_job->remaining_deps);
        
        if (next_job->remaining_deps == 0) {
            pthread_cond_signal(&dag.job_completed_cond);
            printf("  %s: %s теперь готов к запуску\n", 
                   job->name, next_job->name);
        }
    }
    
    pthread_cond_signal(&dag.job_completed_cond);
    pthread_mutex_unlock(&dag.running_mutex);
    
    return NULL;
}

//Основная функция выполнения DAG
bool execute_dag(void) {
    //Инициализация
    pthread_mutex_init(&dag.running_mutex, NULL);
    pthread_cond_init(&dag.job_completed_cond, NULL);
    dag.running_jobs = 0;
    dag.dag_failed = false;
    
    pthread_t threads[MAX_JOBS];
    int thread_count = 0;
    bool thread_created[MAX_JOBS] = {false};
    
    printf("\nНачало выполнения DAG\n");
    
    //Начальный запуск задач без зависимостей
    pthread_mutex_lock(&dag.running_mutex);
    
    for (int i = 0; i < dag.job_count; i++) {
        Job* job = &dag.jobs[i];
        if (job->remaining_deps == 0 && !job->completed && !thread_created[i]) {
            //Ждем, если достигнут лимит параллельных задач
            while (dag.running_jobs >= dag.max_concurrent && !dag.dag_failed) {
                printf("Достигнут лимит параллельных задач (%d), ожидаем...\n", 
                       dag.max_concurrent);
                pthread_cond_wait(&dag.job_completed_cond, &dag.running_mutex);
            }
            
            if (dag.dag_failed) {
                printf("DAG остановлен из-за ошибки\n");
                pthread_mutex_unlock(&dag.running_mutex);
                break;
            }
            
            dag.running_jobs++;
            thread_created[i] = true;
            printf("Создаем поток для задачи %s (запущено: %d/%d)\n", 
                   job->name, dag.running_jobs, dag.max_concurrent);
            pthread_create(&threads[thread_count++], NULL, execute_job, job);
        }
    }
    
    pthread_mutex_unlock(&dag.running_mutex);
    
    //Основной цикл ожидания и запуска новых задач
    bool active = true;
    while (active && !dag.dag_failed) {
        //Проверяем, остались ли задачи для запуска
        pthread_mutex_lock(&dag.running_mutex);
        
        bool new_tasks = false;
        for (int i = 0; i < dag.job_count; i++) {
            Job* job = &dag.jobs[i];
            if (!job->completed && job->remaining_deps == 0 && !thread_created[i]) {
                new_tasks = true;
                
                //Запускаем следующую задачу
                while (dag.running_jobs >= dag.max_concurrent && !dag.dag_failed) {
                    pthread_cond_wait(&dag.job_completed_cond, &dag.running_mutex);
                }
                
                if (!dag.dag_failed) {
                    dag.running_jobs++;
                    thread_created[i] = true;
                    printf("Запускаем отложенную задачу %s (запущено: %d/%d)\n", 
                           job->name, dag.running_jobs, dag.max_concurrent);
                    pthread_create(&threads[thread_count++], NULL, execute_job, job);
                }
            }
        }
        
        //Проверяем, все ли задачи завершены
        bool all_done = true;
        for (int i = 0; i < dag.job_count; i++) {
            if (!dag.jobs[i].completed) {
                all_done = false;
                break;
            }
        }
        
        if (all_done || dag.dag_failed || (!new_tasks && dag.running_jobs == 0)) {
            active = false;
        }
        
        pthread_mutex_unlock(&dag.running_mutex);
        
        //Небольшая задержка для уменьшения нагрузки на CPU
        if (active) {
            usleep(10000); //10ms
        }
    }
    
    //Ожидаем завершения всех потоков
    printf("\nОжидаем завершения всех потоков...\n");
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    //Очистка
    pthread_mutex_destroy(&dag.running_mutex);
    pthread_cond_destroy(&dag.job_completed_cond);
    
    //Освобождаем мьютексы
    for (int i = 0; i < dag.mutex_count; i++) {
        pthread_mutex_destroy(&dag.mutexes[i].mutex);
    }
    
    //Проверка результатов
    if (dag.dag_failed) {
        printf("\nDAG прерван из-за ошибки в одной из задач\n");
        return false;
    }
    
    //Проверяем, что все задачи завершены
    for (int i = 0; i < dag.job_count; i++) {
        if (!dag.jobs[i].completed) {
            printf("\nОшибка: задача %s не была выполнена\n", dag.jobs[i].name);
            return false;
        }
    }
    
    printf("\nВсе задачи успешно завершены\n");
    return true;
}

//Основная функция
int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Использование: %s <config.yaml>\n", argv[0]);
        fprintf(stderr, "Пример: %s config.yaml\n", argv[0]);
        return 1;
    }
    
    printf("Загрузка конфигурации из %s...\n", argv[1]);
    
    //
    if (!parse_yaml_config_simple(argv[1])) {
        fprintf(stderr, "Ошибка парсинга конфигурации\n");
        return 1;
    }
    
    //Построение графа зависимостей
    if (!build_dependency_graph()) {
        fprintf(stderr, "Ошибка построения графа зависимостей\n");
        return 1;
    }
    
    //Валидация DAG
    if (!validate_dag()) {
        fprintf(stderr, "DAG некорректен\n");
        return 1;
    }
    
    //Выполнение DAG
    if (!execute_dag()) {
        printf("Выполнение DAG завершилось с ошибкой\n");
        return 1;
    }
    
    printf("\nПрограмма завершена успешно\n");
    return 0;
}