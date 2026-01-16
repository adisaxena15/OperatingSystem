
#include "syscall.h"
#include "string.h"
#define STDIN 0
#define STDOUT 1
#define CONSOLEOUT 2
#define BUFSIZE 4096
#define MAXARGS 64
int is_whitespace(char c) {
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}
void main(int argc, char* argv[]) {
    char buf[BUFSIZE];
    char* newargv[MAXARGS];
    int newargc;
    long bytes_read;
    int i;
    int in_arg;
    char fullpath[256];
    int progfd;
    int result;
    if (argc < 2) {
        _write(CONSOLEOUT, "xargs: missing command\n", 23);
        _exit();
    }
    newargc = 0;
    for (i = 1; i < argc && newargc < MAXARGS - 1; i++) {
        newargv[newargc++] = argv[i];
    }
    bytes_read = _read(STDIN, buf, BUFSIZE - 1);
    if (bytes_read < 0) {
        _write(CONSOLEOUT, "xargs: read error\n", 18);
        _exit();
    }
    buf[bytes_read] = '\0';  
    in_arg = 0;
    for (i = 0; i < bytes_read && newargc < MAXARGS - 1; i++) {
        if (is_whitespace(buf[i])) {
            if (in_arg) {
                buf[i] = '\0';  
                in_arg = 0;
            }
        } else {
            if (!in_arg) {
                newargv[newargc++] = &buf[i];
                in_arg = 1;
            }
        }
    }
    newargv[newargc] = NULL;
    if (newargc <= 1) {
        _exit();
    }
    int child = _fork();
    if (child == 0) {
        char* cmd = newargv[0];
        char* slash = cmd;
        while (*slash && *slash != '/') slash++;
        if (*slash == '/') {
            snprintf(fullpath, 256, "%s", cmd);
        } else {
            snprintf(fullpath, 256, "c/%s", cmd);
        }
        progfd = 3;
        result = _open(progfd, fullpath);
        if (result < 0) {
            _write(CONSOLEOUT, "xargs: cannot open '", 20);
            _write(CONSOLEOUT, fullpath, strlen(fullpath));
            _write(CONSOLEOUT, "'\n", 2);
            _exit();
        }
        _exec(progfd, newargc, newargv);
        _write(CONSOLEOUT, "xargs: exec failed\n", 19);
        _exit();
    } else {
        _wait(child);
    }
    _exit();
}
