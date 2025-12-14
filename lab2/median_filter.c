#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//Структура для передачи данных в поток
typedef struct {
    int **input;
    int **output;
    int rows;
    int cols;
    int window_size;
    int start_row;
    int end_row;
    int iteration;
    int thread_id;
    int *threads_completed;
    pthread_mutex_t *completed_mutex;
    pthread_cond_t *all_done_cond;
} ThreadData;

//Глобальные переменные для синхронизации
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int active_threads = 0;
int max_threads = 0;

//Функция сравнения для qsort
int compare(const void *a, const void *b) {
    return (*(int*)a - *(int*)b);
}

//Функция для получения медианного значения для окрестности пикселя
int get_median(int **matrix, int row, int col, int window_size, int rows, int cols) {
    int *values = malloc(window_size * window_size * sizeof(int));
    int half = window_size / 2;
    int count = 0;
    
    //Собираем значения из окрестности
    for (int i = -half; i <= half; i++) {
        for (int j = -half; j <= half; j++) {
            int r = row + i;
            int c = col + j;
            
            //Проверка границ
            if (r >= 0 && r < rows && c >= 0 && c < cols) {
                values[count++] = matrix[r][c];
            }
        }
    }
    
    //Сортируем и находим медиану
    if (count > 0) {
        qsort(values, count, sizeof(int), compare);
        int median = values[count / 2];
        free(values);
        return median;
    }
    
    free(values);
    return matrix[row][col];
}

//Функция, выполняемая в каждом потоке
void* process_rows(void *arg) {
    ThreadData *data = (ThreadData*)arg;
    
    //Блокировка для подсчета активных потоков
    pthread_mutex_lock(&mutex);
    active_threads++;
    printf("Поток %d начал работу. Активных потоков: %d\n", 
           data->thread_id, active_threads);
    pthread_mutex_unlock(&mutex);
    
    //Обработка назначенных строк
    for (int i = data->start_row; i <= data->end_row; i++) {
        for (int j = 0; j < data->cols; j++) {
            data->output[i][j] = get_median(data->input, i, j, 
                                           data->window_size, 
                                           data->rows, data->cols);
        }
    }
    
    //Отметка о завершении работы потока
    pthread_mutex_lock(data->completed_mutex);
    (*data->threads_completed)++;
    
    //Если все потоки завершили работу, отправляем сигнал
    if (*data->threads_completed == max_threads) {
        pthread_cond_broadcast(data->all_done_cond);
    }
    pthread_mutex_unlock(data->completed_mutex);
    
    //Ожидание, пока все потоки завершат текущую итерацию
    pthread_mutex_lock(data->completed_mutex);
    while (*data->threads_completed < max_threads) {
        pthread_cond_wait(data->all_done_cond, data->completed_mutex);
    }
    pthread_mutex_unlock(data->completed_mutex);
    
    //Блокировка для уменьшения счетчика активных потоков
    pthread_mutex_lock(&mutex);
    active_threads--;
    printf("Поток %d завершил итерацию %d. Активных потоков: %d\n", 
           data->thread_id, data->iteration, active_threads);
    pthread_mutex_unlock(&mutex);
    
    return NULL;
}

//Функция для создания матрицы
int** create_matrix(int rows, int cols) {
    int **matrix = (int**)malloc(rows * sizeof(int*));
    for (int i = 0; i < rows; i++) {
        matrix[i] = (int*)calloc(cols, sizeof(int));
    }
    return matrix;
}

//Функция для освобождения памяти матрицы
void free_matrix(int **matrix, int rows) {
    for (int i = 0; i < rows; i++) {
        free(matrix[i]);
    }
    free(matrix);
}

//Функция для вывода матрицы
void print_matrix(int **matrix, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            printf("%4d ", matrix[i][j]);
        }
        printf("\n");
    }
}

//Функция для генерации случайной матрицы
void generate_random_matrix(int **matrix, int rows, int cols) {
    srand(time(NULL));
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            matrix[i][j] = rand() % 100;
        }
    }
}

//Функция для копирования матрицы
void copy_matrix(int **src, int **dst, int rows, int cols) {
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            dst[i][j] = src[i][j];
        }
    }
}

int main(int argc, char *argv[]) {
    //Проверка аргументов командной строки
    if (argc < 4) {
        return 1;
    }
    
    //Чтение параметров
    max_threads = atoi(argv[1]);
    int window_size = atoi(argv[2]);
    int K = atoi(argv[3]);
    
    if (max_threads <= 0 || window_size % 2 == 0 || window_size < 3 || K <= 0) {
        printf("Ошибка: некорректные параметры!\n");
        printf("  max_threads должно быть > 0\n");
        printf("  window_size должно быть нечетным числом >= 3\n");
        printf("  K должно быть > 0\n");
        return 1;
    }
    
    //Размеры матрицы
    int rows = 20;
    int cols = 20;
    
    //Проверка, что потоков не больше, чем строк
    if (max_threads > rows) {
        max_threads = rows;
        printf("Внимание: уменьшено количество потоков до %d (количество строк)\n", max_threads);
    }
    
    //Создание матриц
    int **current = create_matrix(rows, cols);
    int **temp = create_matrix(rows, cols);
    
    //Генерация исходной матрицы
    generate_random_matrix(current, rows, cols);
    
    printf("Максимальное количество потоков: %d\n", max_threads);
    printf("Размер окна: %d\n", window_size);
    printf("Количество итераций: %d\n", K);
    printf("Размер матрицы: %d x %d\n", rows, cols);
    printf("PID процесса: %d\n", getpid());
    
    printf("\nИсходная матрица:\n");
    print_matrix(current, rows, cols);
    
    //Основной цикл итераций
    for (int iter = 0; iter < K; iter++) {
        printf("Итерация %d\n", iter + 1);
        
        //Создание переменных для синхронизации
        int threads_completed = 0;
        pthread_mutex_t completed_mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t all_done_cond = PTHREAD_COND_INITIALIZER;
        
        //Создание потоков
        pthread_t threads[max_threads];
        ThreadData thread_data[max_threads];
        
        //Расчет строк для каждого потока
        int rows_per_thread = rows / max_threads;
        int remaining_rows = rows % max_threads;
        int current_row = 0;
        
        //Подготовка данных для потоков
        for (int i = 0; i < max_threads; i++) {
            thread_data[i].input = current;
            thread_data[i].output = temp;
            thread_data[i].rows = rows;
            thread_data[i].cols = cols;
            thread_data[i].window_size = window_size;
            thread_data[i].iteration = iter + 1;
            thread_data[i].thread_id = i;
            thread_data[i].threads_completed = &threads_completed;
            thread_data[i].completed_mutex = &completed_mutex;
            thread_data[i].all_done_cond = &all_done_cond;
            
            //Распределение строк
            thread_data[i].start_row = current_row;
            int extra = (i < remaining_rows) ? 1 : 0;
            thread_data[i].end_row = current_row + rows_per_thread + extra - 1;
            current_row = thread_data[i].end_row + 1;
            
            printf("Поток %d обрабатывает строки %d-%d\n", 
                   i, thread_data[i].start_row, thread_data[i].end_row);
        }
        
        //Создание потоков
        for (int i = 0; i < max_threads; i++) {
            pthread_create(&threads[i], NULL, process_rows, &thread_data[i]);
            
            //Небольшая задержка для наглядного порядка создания потоков
            usleep(1000);
        }
        
        //Ожидание завершения всех потоков
        for (int i = 0; i < max_threads; i++) {
            pthread_join(threads[i], NULL);
        }
        
        //Копирование результата обратно в current
        copy_matrix(temp, current, rows, cols);
        
        //Уничтожение мьютекса и условной переменной
        pthread_mutex_destroy(&completed_mutex);
        pthread_cond_destroy(&all_done_cond);
        
        printf("\nМатрица после итерации %d:\n", iter + 1);
        print_matrix(current, rows, cols);
    }
    
    //Освобождение памяти
    free_matrix(current, rows);
    free_matrix(temp, rows);
    
    return 0;
}

