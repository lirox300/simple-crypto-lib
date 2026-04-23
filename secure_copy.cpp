#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <pthread.h>
#include <queue>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#define WORKERS_COUNT 4

extern "C" {
void cezare_key(char key);
void cezare(void *src, void *dst, int len);
}

volatile int keep_running = 1;

void handle_sigint(int sig) { keep_running = 0; }

struct SharedData {
    std::queue<std::string> files;
    std::string out_dir;
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

void *worker(void *arg) {
    SharedData *shared = (SharedData *)arg;
    pthread_t tid = pthread_self();

    while (keep_running) {
        lock_mutex(&shared->mutex, tid);
        if (!keep_running || shared->files.empty()) {
            pthread_mutex_unlock(&shared->mutex);
            break;
        }
        std::string file = shared->files.front();
        shared->files.pop();
        pthread_mutex_unlock(&shared->mutex);

        std::string out_path = shared->out_dir + "/" + get_base(file);

        std::ifstream in(file, std::ios::binary);
        std::ofstream out(out_path, std::ios::binary);
        bool success = (in && out);

        if (success) {
            char buffer[4096];
            while (keep_running && in) {
                in.read(buffer, sizeof(buffer));
                int bytes_read = in.gcount();
                if (bytes_read > 0) {
                    cezare(buffer, buffer, bytes_read);
                    out.write(buffer, bytes_read);
                }
            }
            if (!keep_running)
                success = false;
        }

        if (in)
            in.close();
        if (out)
            out.close();

        if (!success)
            unlink(out_path.c_str());

        if (success && keep_running) {
            lock_mutex(&shared->mutex, tid);
            std::ofstream log("log.txt", std::ios::app);
            if (log) {
                time_t now = time(NULL);
                char t_buf[64];
                std::strftime(t_buf, sizeof(t_buf), "%Y-%m-%d %H:%M:%S",
                              std::localtime(&now));
                log << "[" << t_buf << "] Thread: " << (unsigned long)tid
                    << ", File: " << file << "\n";
            }
            pthread_mutex_unlock(&shared->mutex);
        }
    }
    return nullptr;
}

double run_processing(int num_threads, const std::vector<std::string> &files,
                      const std::string &out_dir) {
    SharedData shared;
    shared.out_dir = out_dir;
    pthread_mutex_init(&shared.mutex, NULL);

    for (size_t i = 0; i < files.size(); ++i) {
        shared.files.push(files[i]);
    }

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    std::vector<pthread_t> threads(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        pthread_create(&threads[i], NULL, worker, &shared);
    }

    for (int i = 0; i < num_threads; ++i) {
        pthread_join(threads[i], NULL);
    }

    clock_gettime(CLOCK_MONOTONIC, &end);
    pthread_mutex_destroy(&shared.mutex);

    return (end.tv_sec - start.tv_sec) * 1000.0 +
           (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

void print_stats(const std::string &mode_name, double total_ms, int count) {
    std::cout << "СТАТИСТИКА (" << mode_name << ")\n";
    std::cout << "Обработано файлов: " << count << "\n";
    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Общее время: " << total_ms << " мс\n";
    std::cout << "Среднее время на файл: " << (count > 0 ? total_ms / count : 0)
              << " мс\n";
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " [--mode=sequential|--mode=parallel] file1.txt "
                     "[file2.txt...] output_dir/ key\n";
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    int mode = 0;
    int arg_start = 1;
    std::string first_arg = argv[1];

    if (first_arg == "--mode=sequential") {
        mode = 1;
        arg_start = 2;
    } else if (first_arg == "--mode=parallel") {
        mode = 2;
        arg_start = 2;
    }

    if (argc - arg_start < 3) {
        std::cerr << "Ошибка: недостаточно аргументов файлов/папки/ключа.\n";
        return 1;
    }

    char key = argv[argc - 1][0];
    cezare_key(key);

    std::string out_dir = argv[argc - 2];
    struct stat st = {0};
    if (stat(out_dir.c_str(), &st) == -1) {
        mkdir(out_dir.c_str(), 0700);
    }

    std::vector<std::string> files;
    for (int i = arg_start; i < argc - 2; ++i) {
        files.push_back(argv[i]);
    }
    int files_count = files.size();

    if (mode == 1) {
        double t = run_processing(1, files, out_dir);
        if (keep_running)
            print_stats("SEQUENTIAL", t, files_count);
    } else if (mode == 2) {
        double t = run_processing(WORKERS_COUNT, files, out_dir);
        if (keep_running)
            print_stats("PARALLEL", t, files_count);
    } else {
        std::cout << "[Режим AUTO] Количество файлов: " << files_count << "\n";

        if (files_count < 5) {
            std::cout << "Эвристика: выбрано SEQUENTIAL (< 5 файлов)\n\n";
        } else {
            std::cout << "Эвристика: выбрано PARALLEL (>= 5 файлов)\n\n";
        }

        double seq_t = run_processing(1, files, out_dir);
        if (!keep_running)
            goto end;

        double par_t = run_processing(WORKERS_COUNT, files, out_dir);
        if (!keep_running)
            goto end;

        print_stats("SEQUENTIAL", seq_t, files_count);
        std::cout << "\n";
        print_stats("PARALLEL", par_t, files_count);
        std::cout << std::fixed << std::setprecision(3);
        if (seq_t > par_t) {
            std::cout << "\nПАРАЛЛЕЛЬНЫЙ режим быстрее в " << (seq_t / par_t)
                      << " раз (выигрыш " << (seq_t - par_t) << " мс)\n";
        } else if (par_t > seq_t) {
            std::cout << "\nПОСЛЕДОВАТЕЛЬНЫЙ режим быстрее в "
                      << (par_t / seq_t) << " раз (выигрыш " << (par_t - seq_t)
                      << " мс)\n";
        } else {
            std::cout << "Время выполнения совпадает\n";
        }
    }

end:
    if (!keep_running) {
        std::cout << "Операция прервана пользователем\n";
    }

    return 0;
}
