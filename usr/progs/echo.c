
#include "syscall.h"
#include "string.h"
#define STDOUT 1
#define CONSOLEOUT 2
void main(int argc, char* argv[]) {
    int i;
    for (i = 1; i < argc; i++) {
        _write(STDOUT, argv[i], strlen(argv[i]));
        if (i < argc - 1) {
            _write(STDOUT, " ", 1);
        }
    }
    _write(STDOUT, "\n", 1);
    _exit();
}
