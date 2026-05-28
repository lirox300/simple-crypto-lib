#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

static char *page_addr = nullptr;
static size_t page_size = 0;
static size_t stored_key_len = 0;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

static void cleanup_secure_memory() {
    if (page_addr && page_addr != MAP_FAILED) {
        mprotect(page_addr, page_size, PROT_READ | PROT_WRITE);
        volatile char *p = (volatile char *)page_addr;
        for (size_t i = 0; i < page_size; ++i) {
            p[i] = 0;
        }
        munmap(page_addr, page_size);
        page_addr = nullptr;
    }
}

extern "C" {
void set_master_key(const char *key, int len) {
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

    stored_key_len = (size_t)len < 256 ? (size_t)len : 256;
    memcpy(page_addr, key, stored_key_len);
    mprotect(page_addr, page_size, PROT_NONE);

    atexit(cleanup_secure_memory);
    pthread_mutex_unlock(&mtx);
}

void crypt_rc4(const char *salt, void *data, int len) {
    if (!page_addr || page_addr == MAP_FAILED || len <= 0 || !data)
        return;

    unsigned char k_buf[256 + 16];
    size_t k_len = stored_key_len + 16;
    if (k_len > sizeof(k_buf))
        k_len = sizeof(k_buf);

    bool key_copied = false;

    pthread_mutex_lock(&mtx);
    if (mprotect(page_addr, page_size, PROT_READ) == 0) {
        memcpy(k_buf, page_addr, stored_key_len);
        mprotect(page_addr, page_size, PROT_NONE);
        key_copied = true;
    }
    pthread_mutex_unlock(&mtx);
    if (!key_copied)
        return;
    memcpy(k_buf + stored_key_len, salt, 16);

    unsigned char S[256];
    for (int i = 0; i < 256; ++i)
        S[i] = i;
    int j = 0;
    for (int i = 0; i < 256; ++i) {
        j = (j + S[i] + k_buf[i % k_len]) % 256;
        unsigned char tmp = S[i];
        S[i] = S[j];
        S[j] = tmp;
    }

    volatile char *p_k = (volatile char *)k_buf;
    for (size_t i = 0; i < sizeof(k_buf); ++i)
        p_k[i] = 0;

    unsigned char *d = (unsigned char *)data;
    int i_idx = 0, j_idx = 0;

    for (int k = 0; k < len; ++k) {
        i_idx = (i_idx + 1) % 256;
        j_idx = (j_idx + S[i_idx]) % 256;
        unsigned char tmp = S[i_idx];
        S[i_idx] = S[j_idx];
        S[j_idx] = tmp;
        d[k] ^= S[(S[i_idx] + S[j_idx]) % 256];
    }

    volatile char *p_s = (volatile char *)S;
    for (size_t i = 0; i < sizeof(S); ++i)
        p_s[i] = 0;
}

void *get_secure_page() { return page_addr; }
}
