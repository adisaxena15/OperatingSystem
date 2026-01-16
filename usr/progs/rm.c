
#include "syscall.h"
#include "string.h"
#define CONSOLEOUT 2
void main(int argc, char* argv[]) {
    int i;
    int result;
    
    if (argc < 2) {
        _write(CONSOLEOUT, "rm: missing operand\n", 20);
        _exit();
    }
    for (i = 1; i < argc; i++) {
        result = _fsdelete(argv[i]);
        if (result < 0) {
            _write(CONSOLEOUT, "rm: cannot remove '", 19);
            _write(CONSOLEOUT, argv[i], strlen(argv[i]));
            _write(CONSOLEOUT, "': No such file\n", 16);
        }
    }
    _exit();
}
