char current_key = 0;

extern "C" {
void cezare_key(char key) { current_key = key; }

void cezare(void* src, void* dst, int len) {
    if (!src || !dst || len <= 0) return;

    char* s = static_cast<char*>(src);
    char* d = static_cast<char*>(dst);

    for (int i = 0; i < len; ++i) { d[i] = s[i] ^ current_key; }
}
}
