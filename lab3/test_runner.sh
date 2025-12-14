#!/bin/bash
# test_runner.sh

echo "Запуск тестов"

# Компиляция
make clean
make

# Тест 1: Нормальная работа
echo -e "\nТест 1: Нормальная работа"
echo "10 20 30" > test1.txt
echo "5.5 15.5 25.5" >> test1.txt
./parent test1.txt

# Тест 2: Файл не существует
echo -e "\nТест 2: Файл не существует"
./parent nonexistent.txt

# Тест 3: Нет прав на чтение
echo -e "\nТест 3: Нет прав на чтение"
echo "test" > norights.txt
chmod 000 norights.txt
./parent norights.txt
chmod 644 norights.txt

# Тест 4: Пустой файл
echo -e "\nТест 4: Пустой файл"
touch empty.txt
./parent empty.txt

# Тест 5: Очень длинные строки
echo -e "\nТест 5: Длинные строки"
for i in {1..100}; do
    echo -n "$i " >> long.txt
done
echo >> long.txt
./parent long.txt

# Тест 6: Специальные символы
echo -e "\nТест 6: Специальные символы"
echo -e "10\t20\n30 40" > special.txt
./parent special.txt

# Тест 7: Большой файл
echo -e "\nТест 7: Большой файл"
for i in {1..10000}; do
    echo "$i $((i+1)) $((i+2))" >> big.txt
done
timeout 10 ./parent big.txt  # Ограничение по времени

# Тест 8: Многократный запуск
echo -e "\nТест 8: Параллельный запуск"
for i in {1..5}; do
    ./parent test1.txt &
done
wait

# Тест 9: child.c отсутствует
echo -e "\nТест 9: child отсутствует"
mv child child.txt
./parent test1.txt
mv child.txt child

# Очистка
rm -f test1.txt norights.txt empty.txt long.txt special.txt big.txt
echo -e "\nТесты завершены"
