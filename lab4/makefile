CC = gcc
CFLAGS = -Wall -Wextra -fPIC
LDFLAGS = -ldl -lm

all: libs program1 program2

# Сборка библиотек
libs: libimplementation1.so libimplementation2.so libimplementation1.a libimplementation2.a

# Динамические библиотеки
libimplementation1.so: lib1.o
	$(CC) -shared -o libimplementation1.so lib1.o -lm

libimplementation2.so: lib2.o
	$(CC) -shared -o libimplementation2.so lib2.o -lm

# Статические библиотеки
libimplementation1.a: lib1.o
	ar rcs libimplementation1.a lib1.o

libimplementation2.a: lib2.o
	ar rcs libimplementation2.a lib2.o

lib1.o: lib1.c contracts.h
	$(CC) $(CFLAGS) -c lib1.c -o lib1.o

lib2.o: lib2.c contracts.h
	$(CC) $(CFLAGS) -c lib2.c -o lib2.o

# Сборка программы 1 (с линковкой на этапе компиляции)
program1: program1.c lib1.o
	$(CC) $(CFLAGS) -o program1 program1.c lib1.o -lm

# Сборка программы 2 (динамическая загрузка)
program2: program2.c
	$(CC) $(CFLAGS) -o program2 program2.c $(LDFLAGS)

clean:
	rm -f *.o *.so *.a program1 program2

.PHONY: all clean test