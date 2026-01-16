
#include "syscall.h"
#include "string.h"
#define STDIN 0
#define STDOUT 1
#define CONSOLEOUT 2
#define BUFSIZE 4096
void print_counts(unsigned long lines, unsigned long words, unsigned long bytes) {
    char out[64];
    int n = snprintf(out, sizeof(out), "%lu\t%lu\t%lu\n", lines, words, bytes);
    if (n > 0) {
        _write(STDOUT, out, (unsigned long)n);
    }
}
void count_file(int fd) {
    char buf[BUFSIZE];
    long bytes_read;
    unsigned long lines = 0;
    unsigned long words = 0;
    unsigned long bytes = 0;
    int in_word = 0;
    int i;
    while ((bytes_read = _read(fd, buf, BUFSIZE)) > 0) {
        for (i = 0; i < bytes_read; i++) {
            char c = buf[i];
            bytes++;
            if (c == '\n') {
                lines++;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                in_word = 0;
            } else {
                if (!in_word) {
                    words++;
                    in_word = 1;
                }
            }
        }
    }
    print_counts(lines, words, bytes);
}
void main(int argc, char* argv[]) {
    int fd;
    int result;
    if (argc < 2) {
        count_file(STDIN);
    } else {
        fd = 3;
        result = _open(fd, argv[1]);
        if (result < 0) {
            _write(CONSOLEOUT, "wc: ", 4);
            _write(CONSOLEOUT, argv[1], strlen(argv[1]));
            _write(CONSOLEOUT, ": No such file\n", 15);
            _exit();
        }
        count_file(fd);
        _close(fd);
    }
    _exit();
}
