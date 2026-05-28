#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <queue>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define WORKERS_COUNT 4

extern "C" {
void set_master_key(const char *key, int len);
void crypt_rc4(const char *salt, void *data, int len);
void *get_secure_page();
}

volatile int keep_running = 1;

void handle_sigint(int sig) { keep_running = 0; }

void handle_secure_violation(int sig) {
    std::cout << "Сигнал " << (sig == SIGSEGV ? "SIGSEGV" : "SIGBUS")
              << " перехвачен.\n";
    std::cout << "Изоляция памяти PROT_NONE активна.\n";
    exit(0);
}

struct FileTask {
    std::string src_path;
    std::string archive_name;
    uint32_t file_size;
    uint32_t name_len;
    size_t img_offset;
    char salt[16];
};

struct SharedData {
    std::queue<FileTask> tasks;
    char *img_ptr;
    pthread_mutex_t mutex;
};

void lock_mutex(pthread_mutex_t *mutex, pthread_t tid) {
    time_t start = time(NULL);
    while (keep_running) {
        int rc = pthread_mutex_trylock(mutex);
        if (rc == 0)
            return;
        if (rc == EBUSY) {
            if (time(NULL) - start >= 5) {
                std::cerr << "Возможная взаимоблокировка: поток "
                          << (unsigned long)tid
                          << " ожидает мьютекс более 5 секунд\n";
                exit(1);
            } else {
                usleep(50000);
            }
        } else {
            break;
        }
    }
}

std::string get_base(const std::string &path) {
    size_t pos = path.find_last_of("/");
    if (pos == std::string::npos) {
        return path;
    } else {
        return path.substr(pos + 1);
    }
}

void generate_salt(char *salt) {
    for (int i = 0; i < 16; ++i)
        salt[i] = (char)(rand() % 256);
}

void *worker(void *arg) {
    SharedData *shared = (SharedData *)arg;
    pthread_t tid = pthread_self();

    while (keep_running) {
        lock_mutex(&shared->mutex, tid);
        if (!keep_running || shared->tasks.empty()) {
            pthread_mutex_unlock(&shared->mutex);
            break;
        }
        FileTask task = shared->tasks.front();
        shared->tasks.pop();
        pthread_mutex_unlock(&shared->mutex);

        std::ifstream in(task.src_path, std::ios::binary);
        if (!in)
            continue;

        char *ptr = shared->img_ptr + task.img_offset;

        memcpy(ptr, &task.file_size, 4);
        memcpy(ptr + 4, &task.name_len, 4);
        memcpy(ptr + 8, task.salt, 16);
        memcpy(ptr + 24, task.archive_name.c_str(), task.name_len);

        if (task.file_size > 0) {
            in.read(ptr + 24 + task.name_len, task.file_size);
            crypt_rc4(task.salt, ptr + 24 + task.name_len, task.file_size);
        }

        if (in)
            in.close();

        if (keep_running) {
            lock_mutex(&shared->mutex, tid);
            std::ofstream log("log.txt", std::ios::app);
            if (log) {
                time_t now = time(NULL);
                char t_buf[64];
                std::strftime(t_buf, sizeof(t_buf), "%Y-%m-%d %H:%M:%S",
                              std::localtime(&now));
                log << "[" << t_buf << "] Thread: " << (unsigned long)tid
                    << ", File: " << task.archive_name << "\n";
            }
            pthread_mutex_unlock(&shared->mutex);
        }
    }
    return nullptr;
}

void traverse_dir(const std::string &base_path,
                  const std::string &current_rel_path,
                  std::vector<FileTask> &tasks, size_t &current_offset) {
    std::string full_path = base_path;
    if (!current_rel_path.empty())
        full_path += "/" + current_rel_path;

    DIR *dir = opendir(full_path.c_str());
    if (!dir)
        return;

    struct dirent *entry;
    while (keep_running && (entry = readdir(dir)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..")
            continue;

        std::string rel_path =
            current_rel_path.empty() ? name : current_rel_path + "/" + name;
        std::string f_path = base_path + "/" + rel_path;

        struct stat st;
        if (stat(f_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                traverse_dir(base_path, rel_path, tasks, current_offset);
            } else if (S_ISREG(st.st_mode)) {
                FileTask task;
                task.src_path = f_path;
                task.archive_name = rel_path;
                task.file_size = (uint32_t)st.st_size;
                task.name_len = (uint32_t)rel_path.size();
                task.img_offset = current_offset;
                generate_salt(task.salt);

                current_offset += 24 + task.name_len + task.file_size;
                tasks.push_back(task);
            }
        }
    }
    closedir(dir);
}

void run_add(const std::string &img_path, const std::string &key,
             const std::vector<std::string> &inputs) {
    set_master_key(key.c_str(), key.size());
    srand(time(NULL));

    size_t current_offset = 0;
    struct stat img_st;
    if (stat(img_path.c_str(), &img_st) == 0) {
        current_offset = img_st.st_size;
    }

    std::vector<FileTask> tasks;
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::string input_path = inputs[i];
        struct stat st;
        if (stat(input_path.c_str(), &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                traverse_dir(input_path, "", tasks, current_offset);
            } else if (S_ISREG(st.st_mode)) {
                std::string name = get_base(input_path);
                FileTask task;
                task.src_path = input_path;
                task.archive_name = name;
                task.file_size = (uint32_t)st.st_size;
                task.name_len = (uint32_t)name.size();
                task.img_offset = current_offset;
                generate_salt(task.salt);

                current_offset += 24 + task.name_len + task.file_size;
                tasks.push_back(task);
            }
        } else {
            std::cerr << "Предупреждение: '" << input_path << "' не найден.\n";
        }
    }

    if (tasks.empty())
        return;

    int fd = open(img_path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd < 0)
        return;

    if (ftruncate(fd, current_offset) != 0) {
        close(fd);
        return;
    }

    char *img_ptr = (char *)mmap(NULL, current_offset, PROT_READ | PROT_WRITE,
                                 MAP_SHARED, fd, 0);
    close(fd);

    if (img_ptr == MAP_FAILED)
        return;

    SharedData shared;
    shared.img_ptr = img_ptr;
    pthread_mutex_init(&shared.mutex, NULL);

    for (size_t i = 0; i < tasks.size(); ++i) {
        shared.tasks.push(tasks[i]);
    }

    int num_threads =
        (int)tasks.size() < WORKERS_COUNT ? tasks.size() : WORKERS_COUNT;
    std::vector<pthread_t> threads(num_threads);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_threads; ++i) {
        pthread_create(&threads[i], NULL, worker, &shared);
    }

    for (int i = 0; i < num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    munmap(img_ptr, current_offset);
    pthread_mutex_destroy(&shared.mutex);

    double total_ms = (end.tv_sec - start.tv_sec) * 1000.0 +
                      (end.tv_nsec - start.tv_nsec) / 1000000.0;
    std::cout << "СТАТИСТИКА (-add)\n";
    std::cout << "Обработано файлов: " << tasks.size() << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Общее время: " << total_ms << " мс\n";
}

struct Entry {
    std::string name;
    uint32_t size;
};

void run_list(const std::string &img_path) {
    std::ifstream in(img_path, std::ios::binary);
    if (!in) {
        std::cerr << "Ошибка: не удалось открыть образ\n";
        return;
    }

    in.seekg(0, std::ios::end);
    size_t file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<Entry> entries;

    while (keep_running) {
        size_t current_pos = in.tellg();
        if (current_pos >= file_size)
            break;

        if (current_pos + 24 > file_size) {
            std::cerr << "Ошибка: некорректный формат образа\n";
            return;
        }

        uint32_t f_size = 0, name_len = 0;
        in.read((char *)&f_size, 4);
        in.read((char *)&name_len, 4);

        if (name_len == 0 || name_len > 4096) {
            std::cerr << "Ошибка: некорректный формат образа (неверная длина "
                         "имени)\n";
            return;
        }

        if (current_pos + 24 + name_len + f_size > file_size) {
            std::cerr << "Ошибка: файл поврежден\n";
            return;
        }

        in.seekg(16, std::ios::cur);

        std::vector<char> name_buf(name_len, 0);
        in.read(name_buf.data(), name_len);

        for (char c : name_buf) {
            if (c >= 0 && c < 32) {
                std::cerr
                    << "Ошибка: некорректный формат образа (мусор в имени)\n";
                return;
            }
        }

        std::string fname(name_buf.data(), name_len);
        entries.push_back({fname, f_size});

        in.seekg(f_size, std::ios::cur);
    }

    if (in)
        in.close();

    std::sort(entries.begin(), entries.end(),
              [](const Entry &a, const Entry &b) { return a.name < b.name; });

    for (size_t i = 0; i < entries.size(); ++i) {
        std::cout << entries[i].name << " (" << entries[i].size << " байт)\n";
    }
}

void run_get(const std::string &img_path, const std::string &key,
             const std::string &out_file, const std::string &target_name) {
    set_master_key(key.c_str(), key.size());

    std::ifstream in(img_path, std::ios::binary);
    if (!in) {
        std::cerr << "Ошибка: не удалось открыть образ\n";
        return;
    }

    in.seekg(0, std::ios::end);
    size_t file_size = in.tellg();
    in.seekg(0, std::ios::beg);

    bool found = false;

    while (keep_running) {
        size_t current_pos = in.tellg();
        if (current_pos >= file_size)
            break;

        if (current_pos + 24 > file_size) {
            std::cerr << "Ошибка: некорректный формат образа\n";
            return;
        }

        uint32_t f_size = 0, name_len = 0;
        in.read((char *)&f_size, 4);
        in.read((char *)&name_len, 4);

        if (name_len == 0 || name_len > 4096) {
            std::cerr << "Ошибка: некорректный формат образа (неверная длина "
                         "имени)\n";
            return;
        }

        if (current_pos + 24 + name_len + f_size > file_size) {
            std::cerr << "Ошибка: файл поврежден\n";
            return;
        }

        char salt[16];
        in.read(salt, 16);

        std::vector<char> name_buf(name_len, 0);
        in.read(name_buf.data(), name_len);

        for (char c : name_buf) {
            if (c >= 0 && c < 32) {
                std::cerr
                    << "Ошибка: некорректный формат образа (мусор в имени)\n";
                return;
            }
        }

        std::string fname(name_buf.data(), name_len);

        if (fname == target_name) {
            found = true;
            std::vector<char> content_buf(f_size);

            if (f_size > 0) {
                in.read(content_buf.data(), f_size);
                crypt_rc4(salt, content_buf.data(), f_size);
            }

            std::ofstream out(out_file, std::ios::binary);
            if (out && f_size > 0) {
                out.write(content_buf.data(), f_size);
            }
            if (out)
                out.close();

            std::cout << "Файл успешно извлечен в: " << out_file << "\n";
            break;
        } else {
            in.seekg(f_size, std::ios::cur);
        }
    }

    if (in)
        in.close();

    if (!found) {
        std::cerr << "Ошибка: файл не найден в образе\n";
    }
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Ошибка: недостаточно аргументов.\n";
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    std::string mode = argv[1];

    if (mode == "-add") {
        std::string key, image;
        std::vector<std::string> inputs;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-key" && i + 1 < argc)
                key = argv[++i];
            else if (arg == "-image" && i + 1 < argc)
                image = argv[++i];
            else
                inputs.push_back(arg);
        }
        if (key.empty() || image.empty() || inputs.empty()) {
            std::cerr << "Ошибка: неверные параметры для -add\n";
            return 1;
        }
        run_add(image, key, inputs);

    } else if (mode == "-list") {
        std::string image;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-image" && i + 1 < argc)
                image = argv[++i];
        }
        if (image.empty()) {
            std::cerr << "Ошибка: неверные параметры для -list\n";
            return 1;
        }
        run_list(image);

    } else if (mode == "-get") {
        std::string key, image, out, file_name;
        for (int i = 2; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-image" && i + 1 < argc)
                image = argv[++i];
            else if (arg == "-key" && i + 1 < argc)
                key = argv[++i];
            else if (arg == "-out" && i + 1 < argc)
                out = argv[++i];
            else
                file_name = arg;
        }
        if (image.empty() || key.empty() || out.empty() || file_name.empty()) {
            std::cerr << "Ошибка: неверные параметры для -get\n";
            return 1;
        }
        run_get(image, key, out, file_name);
    } else {
        std::cerr << "Ошибка: неизвестный режим\n";
    }

    if (!keep_running) {
        std::cout << "Операция прервана пользователем\n";
    } /*else {
        std::cout << "\n[TEST] Проверка аппаратной изоляции...\n";
        void *sec_addr = get_secure_page();
        if (sec_addr) {
            signal(SIGSEGV, handle_secure_violation);
            signal(SIGBUS, handle_secure_violation);
            volatile char test = *((volatile char *)sec_addr);
            (void)test;
            std::cout
                << "Ошибка: чтение из защищенной памяти выполнено успешно.\n";
        }
    }*/

    return 0;
}
