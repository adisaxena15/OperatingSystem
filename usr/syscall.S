// syscall.S - System call function definitions
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

#include "scnum.h"

        .text

        .global _exit
        .type   _exit, @function
_exit:
        li      a7, SYSCALL_EXIT
        ecall
        ret

        .global _exec
        .type   _exec, @function
_exec:
        li      a7, SYSCALL_EXEC
        ecall
        ret

        .global _fork
        .type   _fork, @function
_fork:
        li      a7, SYSCALL_FORK
        ecall
        ret

        .global _wait
        .type   _wait, @function
_wait:
        li      a7, SYSCALL_WAIT
        ecall
        ret

        .global _print
        .type   _print, @function
_print:
        li      a7, SYSCALL_PRINT
        ecall
        ret

        .global _usleep
        .type   _usleep, @function
_usleep:
        li      a7, SYSCALL_USLEEP
        ecall
        ret

        .global _open
        .type   _open, @function
_open:
        li      a7, SYSCALL_OPEN
        ecall
        ret

        .global _fscreate
        .type   _fscreate, @function
_fscreate:
        li      a7, SYSCALL_FSCREATE
        ecall
        ret

        .global _fsdelete
        .type   _fsdelete, @function
_fsdelete:
        li      a7, SYSCALL_FSDELETE
        ecall
        ret

        .global _close
        .type   _close, @function
_close:
        li      a7, SYSCALL_CLOSE
        ecall
        ret

        .global _read
        .type   _read, @function
_read:
        li      a7, SYSCALL_READ
        ecall
        ret

        .global _write
        .type   _write, @function
_write:
        li      a7, SYSCALL_WRITE
        ecall
        ret

        .global _fcntl
        .type   _fcntl, @function
_fcntl:
        li      a7, SYSCALL_FCNTL
        ecall
        ret

        .global _pipe
        .type   _pipe, @function
_pipe:
        li      a7, SYSCALL_PIPE
        ecall
        ret

        .global _uiodup
        .type   _uiodup, @function
_uiodup:
        li      a7, SYSCALL_UIODUP
        ecall
        ret

        .end