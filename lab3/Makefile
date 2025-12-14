CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -D_POSIX_C_SOURCE=200809L -D_GNU_SOURCE
LDFLAGS = -lrt

all: parent child

parent: parent.c
	$(CC) $(CFLAGS) parent.c -o parent $(LDFLAGS)

child: child.c
	$(CC) $(CFLAGS) child.c -o child $(LDFLAGS)

debug: CFLAGS += -g -DDEBUG
debug: parent child

clean:
	rm -f parent child
	-rm -f /dev/shm/child_parent_shm

run: parent child
	@echo "Тест: Базовый запуск"
	@./parent input.txt

verbose: debug
	@echo "Тест: verbose"
	@./parent --verbose input.txt

.PHONY: all clean run verbose debug