#include <math.h>
#include <stdio.h>

//Реализация интеграла методом прямоугольников
float SinIntegral(float A, float B, float e) {
    if (B < A) {
        float temp = A;
        A = B;
        B = temp;
    }
    
    float integral = 0.0;
    int n = (int)((B - A) / e);
    
    for (int i = 0; i < n; i++) {
        float x = A + i * e;
        integral += sin(x) * e;
    }
    
    return integral;
}

//Реализация Пи рядом Лейбница
float Pi(int K) {
    float pi = 0.0;
    
    for (int i = 0; i < K; i++) {
        float term = 1.0 / (2 * i + 1);
        if (i % 2 == 0) {
            pi += term;
        } else {
            pi -= term;
        }
    }
    
    return 4 * pi;
}