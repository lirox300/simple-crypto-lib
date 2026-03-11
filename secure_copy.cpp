#include <csignal>
#include <fstream>
#include <iostream>
#include <pthread.h>
#include <queue>
#include <unistd.h>
#include <vector>

extern "C" {
void cezare_key(char key);
void cezare(void *src, void *dst, int len);
}

volatile int keep_running = 1;

void handle_sigint(int sig) { keep_running = 0; }

struct Chunk {
    std::vector<char> data;
    bool is_eof;
};

struct SharedData {
    std::queue<Chunk> q;
    pthread_mutex_t mutex;
    pthread_cond_t cond_full;
    pthread_cond_t cond_empty;
    bool producer_done;
    const char *in_file;
    const char *out_file;
};

void *producer(void *arg) {
    SharedData *shared = (SharedData *)arg;
    std::ifstream in(shared->in_file, std::ios::binary);
    if (!in) {
        shared->producer_done = true;
        pthread_cond_broadcast(&shared->cond_full);
        return nullptr;
    }

    char buffer[4096];
    while (keep_running && in) {
        in.read(buffer, sizeof(buffer));
        int bytes_read = in.gcount();
        if (bytes_read > 0) {
            cezare(buffer, buffer, bytes_read);

            pthread_mutex_lock(&shared->mutex);
            while (shared->q.size() >= 10 && keep_running) {
                pthread_cond_wait(&shared->cond_empty, &shared->mutex);
            }

            if (!keep_running) {
                pthread_mutex_unlock(&shared->mutex);
                break;
            }

            Chunk chunk;
            chunk.data.assign(buffer, buffer + bytes_read);
            chunk.is_eof = false;
            shared->q.push(chunk);

            pthread_cond_signal(&shared->cond_full);
            pthread_mutex_unlock(&shared->mutex);
        }
    }

    pthread_mutex_lock(&shared->mutex);
    Chunk eof_chunk;
    eof_chunk.is_eof = true;
    shared->q.push(eof_chunk);
    shared->producer_done = true;
    pthread_cond_broadcast(&shared->cond_full);
    pthread_mutex_unlock(&shared->mutex);

    return nullptr;
}

void *consumer(void *arg) {
    SharedData *shared = (SharedData *)arg;
    std::ofstream out(shared->out_file, std::ios::binary);
    if (!out)
        return nullptr;

    while (true) {
        pthread_mutex_lock(&shared->mutex);
        while (shared->q.empty() && keep_running && !shared->producer_done) {
            pthread_cond_wait(&shared->cond_full, &shared->mutex);
        }

        if (!keep_running) {
            pthread_mutex_unlock(&shared->mutex);
            break;
        }

        if (shared->q.empty() && shared->producer_done) {
            pthread_mutex_unlock(&shared->mutex);
            break;
        }

        Chunk chunk = shared->q.front();
        shared->q.pop();

        pthread_cond_signal(&shared->cond_empty);
        pthread_mutex_unlock(&shared->mutex);

        if (chunk.is_eof)
            break;

        out.write(chunk.data.data(), chunk.data.size());
    }

    out.close();

    if (!keep_running) {
        unlink(shared->out_file);
    }

    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " input.txt output.txt key\n";
        return 1;
    }

    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    char key = argv[3][0];
    cezare_key(key);

    SharedData shared;
    shared.in_file = argv[1];
    shared.out_file = argv[2];
    shared.producer_done = false;
    pthread_mutex_init(&shared.mutex, NULL);
    pthread_cond_init(&shared.cond_full, NULL);
    pthread_cond_init(&shared.cond_empty, NULL);

    pthread_t prod_tid, cons_tid;
    pthread_create(&prod_tid, NULL, producer, &shared);
    pthread_create(&cons_tid, NULL, consumer, &shared);

    pthread_join(prod_tid, NULL);

    pthread_mutex_lock(&shared.mutex);
    pthread_cond_broadcast(&shared.cond_full);
    pthread_mutex_unlock(&shared.mutex);

    pthread_join(cons_tid, NULL);

    pthread_mutex_destroy(&shared.mutex);
    pthread_cond_destroy(&shared.cond_full);
    pthread_cond_destroy(&shared.cond_empty);

    if (!keep_running) {
        std::cout << "Операция прервана пользователем\n";
    }

    return 0;
}
