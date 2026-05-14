#include <cstdlib>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

static char *page_addr = nullptr;
static size_t page_size = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void cleanup_secure_memory() {
    if (page_addr && page_addr != MAP_FAILED) {
        mprotect(page_addr, page_size, PROT_READ | PROT_WRITE);
        *(volatile char *)page_addr = 0;
        munmap(page_addr, page_size);
        page_addr = nullptr;
    }
}

extern "C" {
void cezare_key(char key) {
    pthread_mutex_lock(&mtx);
    if (page_addr) {
        pthread_mutex_unlock(&mtx);
        return;
    }

    page_size = sysconf(_SC_PAGESIZE);
    page_addr = (char *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    if (page_addr == MAP_FAILED) {
        pthread_mutex_unlock(&mtx);
        exit(1);
    }

    page_addr[0] = key;
    mprotect(page_addr, page_size, PROT_NONE);

    atexit(cleanup_secure_memory);
    pthread_mutex_unlock(&mtx);
}

void cezare(void *src, void *dst, int len) {
    if (!page_addr || page_addr == MAP_FAILED || len <= 0 || !src || !dst)
        return;

    pthread_mutex_lock(&mtx);
    if (mprotect(page_addr, page_size, PROT_READ) == 0) {
        char *s = (char *)src;
        char *d = (char *)dst;

        for (int i = 0; i < len; ++i) {
            d[i] = s[i] ^ page_addr[0];
        }

        mprotect(page_addr, page_size, PROT_NONE);
    }
    pthread_mutex_unlock(&mtx);
}

void *get_secure_page() { return page_addr; }
}
