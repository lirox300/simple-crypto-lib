# Simple Crypto Lib
Многопоточная утилита и библиотека для безопасного архивирования данных с использованием потокового шифрования RC4
## Сборка библиотеки
**Для macOS:**
```
clang++ -shared -fPIC -o lib.dylib lib.cpp
```
**Для Linux:**
```
g++ -shared -fPIC -o lib.so lib.cpp
```
## Сборка утилиты копирования
**Для macOS:**
```
clang++ -pthread -Wall -o secure_copy secure_copy.cpp ./lib.dylib
```
**Для Linux:**
```
g++ -pthread -Wall -o secure_copy secure_copy.cpp ./lib.so
```
## Запуск
```
./secure_copy -add -key <мастер_ключ> -image <файл_образа> <путь_1> [<путь_2> ... <путь_N>]
```
```
./secure_copy -list -image <файл_образа>
```
```
./secure_copy -get -image <файл_образа> -key <мастер_ключ> -out <выходной_файл> <имя_файла_в_образе>
```
