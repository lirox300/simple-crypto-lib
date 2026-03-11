# Simple Crypto Lib
Простая библиотека на C++ (XOR-шифрование) и консольная многопоточная утилита для безопасного копирования файлов.
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
./secure_copy <входной_файл> <выходной_файл> <символ_ключа>
```
