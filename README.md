# Simple Crypto Lib
Простая библиотека на C++ (XOR-шифрование) и консольное приложение на Python.
## Сборка библиотеки
**Для macOS:**
```
clang++ -shared -fPIC -o lib.dylib lib.cpp
```
**Для Linux:**
```
g++ -shared -fPIC -o lib.so lib.cpp
```
## Запуск
```
python3 app.py ./<библиотека> <символ_ключа> <входной_файл> <выходной_файл>
```
