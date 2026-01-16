
#include "syscall.h"
#include "string.h"
#define STDIN 0
#define STDOUT 1
#define CONSOLEOUT 2
#define BUFSIZE 256

void main(int argc, char* argv[]) {
    char path[BUFSIZE];
    char buf[BUFSIZE];
    int fd;
    int result;
    long bytes_read;
    int len;
    if (argc < 2) {
        snprintf(path, BUFSIZE, "/");
    } else {
        snprintf(path, BUFSIZE, "%s", argv[1]);
    }
    len = strlen(path);
    if (len > 1 && path[len - 1] == '/') {
        path[len - 1] = '\0';
    }
    if (strcmp(path, "/") == 0) {
        _write(STDOUT, "c\n", 2);
        _write(STDOUT, "dev\n", 4);
        _exit();
    }
    fd = 3;
    result = _open(fd, path);
    if (result < 0) {
        _write(CONSOLEOUT, "ls: cannot access '", 19);
        _write(CONSOLEOUT, path, strlen(path));
        _write(CONSOLEOUT, "'\n", 2);
        _exit();
    }
    while ((bytes_read = _read(fd, buf, BUFSIZE - 1)) > 0) {
        _write(STDOUT, buf, bytes_read);
        _write(STDOUT, "\n", 1);
    }
    
    _close(fd);
    _exit();
}
