# Simple Crypto Lib
Простая библиотека и многопоточная утилита на C++ для параллельного XOR-шифрования и безопасного копирования множества файлов.
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
./secure_copy [--mode=sequential|--mode=parallel] <файл_1> [<файл_2> ... <файл_N>] <выходная_директория> <символ_ключа>
```
