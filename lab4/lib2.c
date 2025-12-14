#include <math.h>
#include <stdio.h>

//Реализация интеграла методом трапеций
float SinIntegral(float A, float B, float e) {
    if (B < A) {
        float temp = A;
        A = B;
        B = temp;
    }
    
    float integral = 0.0;
    int n = (int)((B - A) / e);
    
    for (int i = 0; i < n; i++) {
        float x1 = A + i * e;
        float x2 = x1 + e;
        integral += (sin(x1) + sin(x2)) * e / 2.0;
    }
    
    return integral;
}

//Реализация Пи формулой Валлиса
float Pi(int K) {
    float pi = 1.0;
    
    for (int i = 1; i <= K; i++) {
        float numerator = 4.0 * i * i;
        float denominator = 4.0 * i * i - 1.0;
        pi *= (numerator / denominator);
    }
    
    return pi * 2;
}