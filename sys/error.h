/*! @file error.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Error numbers
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _ERROR_H_
#define _ERROR_H_

#define EINVAL 1         ///< Invalid argument
#define EBUSY 2          ///< Device or resource busy
#define ENOTSUP 3        ///< Operation not supported
#define EIO 4            ///< I/O error
#define EBADFMT 5        ///< Bad format
#define ENOENT 6         ///< No such file or directory
#define EACCESS 7        ///< Permission denied
#define EBADFD 8         ///< File descriptor in bad state
#define EMFILE 9         ///< Too many open files
#define EMPROC 10        ///< Too many processes
#define EMTHR 11         ///< Too many threads
#define ECHILD 12        ///< No child process
#define ENOMEM 13        ///< Out of memory
#define EPIPE 14         ///< Broken pipe
#define EEXIST 15        ///< Object exists
#define ENODATABLKS 16   ///< No data blocks
#define ENOINODEBLKS 17  ///< No Inode blocks

// Returns a string with the error name (e.g. 2 => "EBUSY")

extern const char* error_name(int code);

#endif  // _ERROR_H_