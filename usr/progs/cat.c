#include "syscall.h"
#include "string.h"
#define STDIN 0
#define STDOUT 1
#define CONSOLEOUT 2
#define BUFSIZE 4096
void main(int argc, char* argv[]) {
    char buf[BUFSIZE];
    long bytes_read;
    int fd;
    int result;
    if (argc < 2) {
        _write(CONSOLEOUT, "cat: missing file operand\n", 26);
        _exit();
    }
    fd = 3;  
    result = _open(fd, argv[1]);
    if (result < 0) {
        _write(CONSOLEOUT, "cat: ", 5);
        _write(CONSOLEOUT, argv[1], strlen(argv[1]));
        _write(CONSOLEOUT, ": No such file\n", 15);
        _exit();
    }
    while ((bytes_read = _read(fd, buf, BUFSIZE)) > 0) {
        _write(STDOUT, buf, bytes_read);
    }
    _close(fd);
    _exit();
}