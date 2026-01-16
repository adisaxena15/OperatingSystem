// syscall.h - System call function headers
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file syscall.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief System call function headers   
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#ifndef _SYSCALL_H_
#define _SYSCALL_H_

#include <stddef.h>

/**
* @brief Exits the currently running process
* @return Does not return
*/
extern void __attribute__ ((noreturn)) _exit(void);

/**
* @brief Halts currently running user program and starts new program based on opened file at file descriptor.
* @param fd file descripter idx
* @param argc number of arguments in argv
* @param argv array of arguments for multiple args
* @return result of process_exec, else -EBADFD on invalid file descriptors
*/
extern int _exec(int fd, int argc, char ** argv);

/**
* @brief Forks a new child process
* @return 0 for child process, child's TID for parent process
*/
extern int _fork(void);

/**
* @brief Wait for certain child to exit before returning. If tid is the main thread, wait for any child of current thread to exit
* @param tid thread_id
* @return result of thread_join else invalid on invalid thread id
*/
extern int _wait(int tid);

/**
* @brief Prints message to the console
* @param msg Message to be printed
* @return None
*/
extern void _print(const char * msg);

/**
* @brief Sleep for us number of microseconds
* @param us Microsecond amount to sleep for
* @return 0
*/
extern int _usleep(unsigned long us);

/**
* @brief Deletes a file at a specified path
* @param path string path to file
* @return 0 if successful else error code
*/
extern int _fsdelete(char * path);

/**
* @brief Creates a file at a specified path
* @param path string path to file
* @return 0 if successful else error code
*/
extern int _fscreate(char * path);

/**
* @brief Opens a file at the specified file descriptor and returns error code on failure
* @param fd file descriptor number
* @param path string path to file
* @return fd number if sucessful else return error on invalid file descriptor or empty file descriptor
*/
extern int _open(int fd, const char * path);

/**
* @brief Closes the device at the specified file descriptor
* @param fd file descriptor
* @return 0 on success, error on invalid file descriptor or empty file descriptor
*/
extern int _close(int fd);

/**
* @brief Reads from the opened file descriptor and writes bufsz bytes into buf
* @param fd file descriptor number
* @param buf pointer to buffer
* @param bufsz number of bytes to be read
* @return number of bytes read
*/
extern long _read(int fd, void * buf, size_t bufsz);

/**
* @brief Reads bufsz bytes from buf and writes it to the opened file descriptor
* @param fd file descriptor number
* @param buf pointer to buffer
* @param len number of bytes to be written
* @return number of bytes written
*/
extern long _write(int fd, const void * buf, size_t len);

/**
* @brief Performs desired fcntl based on cmd
* @param fd file descriptor number
* @param cmd fcntl command
* @param arg pointer to arguments
* @return fcntl result
*/
extern int _fcntl(int fd, const int cmd, void * arg);

/**
* @brief Opens a pipe at the specified file descriptor and returns error code on failure
* @param wfdptr pointer to write file descriptor
* @param rfdptr pointer to read file descriptor
* @return 0 if successful, return error number on failure
*/
extern int _pipe(int * wfdptr, int * rfdptr);

/**
* @brief Allocates a new file descriptor that refers to the same open file description as the descriptor _oldfd_.
* @param oldfd fd to duplicate
* @param newfd new fd to allocate
* @return fd number if sucessful else return error that occured -EMFILE or -EBADFD
*/
extern int _uiodup(int oldfd, int newfd);

#endif // _SYSCALL_H_