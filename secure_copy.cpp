#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <queue>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

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

        if (!success && !keep_running)
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

int main(int argc, char *argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " file1.txt [file2.txt...] output_dir/ key\n";
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    char key = argv[argc - 1][0];
    cezare_key(key);

    SharedData shared;
    shared.out_dir = argv[argc - 2];
    pthread_mutex_init(&shared.mutex, NULL);

    struct stat st = {0};
    if (stat(shared.out_dir.c_str(), &st) == -1) {
        mkdir(shared.out_dir.c_str(), 0700);
    }

    for (int i = 1; i < argc - 2; ++i) {
        shared.files.push(argv[i]);
    }

    pthread_t threads[3];
    for (int i = 0; i < 3; ++i) {
        pthread_create(&threads[i], NULL, worker, &shared);
    }

    for (int i = 0; i < 3; ++i) {
        pthread_join(threads[i], NULL);
    }

    pthread_mutex_destroy(&shared.mutex);

    if (!keep_running) {
        std::cout << "Операция прервана пользователем\n";
    }

    return 0;
}
