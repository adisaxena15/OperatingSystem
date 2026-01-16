
#include "syscall.h"
#include "string.h"
#define CONSOLEOUT 2
void main(int argc, char* argv[]) {
    int i;
    int result;
    if (argc < 2) {
        _write(CONSOLEOUT, "touch: missing file operand\n", 28);
        _exit();
    }
    for (i = 1; i < argc; i++) {
        int fd = 3;
        result = _open(fd, argv[i]);
        if (result >= 0) {      
            _close(fd);
            continue;
        }
        result = _fscreate(argv[i]);
        if (result < 0) {
            _write(CONSOLEOUT, "touch: cannot create '", 22);
            _write(CONSOLEOUT, argv[i], strlen(argv[i]));
            _write(CONSOLEOUT, "'\n", 2);
        }
    }
    _exit();
}
