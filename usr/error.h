// error.h - Error numbers
//
// Copyright (c) 2024-2025 University of Illinois
// SPDX-License-identifier: NCSA
//

/*! @file error.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Error names
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#ifndef _ERROR_H_
#define _ERROR_H_

/**
 * @brief Invalid argument
 */
#define EINVAL      1
/**
 * @brief Device or resource busy
 */
#define EBUSY       2
/**
 * @brief Operation not supported
 */
#define ENOTSUP     3
/**
 * @brief No such device
 */
#define ENODEV      4
/**
 * @brief I/O error
 */
#define EIO         5
/**
 * @brief Bad format
 */
#define EBADFMT     6
/**
 * @brief No such file or directory
 */
#define ENOENT      7
/**
 * @brief Permission denied
 */
#define EACCESS     8
/**
 * @brief File descriptor in bad state
 */
#define EBADFD      9
/**
 * @brief Too many open files
 */
#define EMFILE     10
/**
 * @brief Too many processes
 */
#define EMPROC     11
/**
 * @brief Too many threads
 */
#define EMTHR      12
/**
 * @brief No child process
 */
#define ECHILD     13
/**
 * @brief Out of memory
 */
#define ENOMEM     14
/**
 * @brief Broken pipe
 */
#define EPIPE      15
/**
 * @brief No data blocks
 */
#define ENODATABLKS  16
/**
 * @brief No Inode blocks
 */
#define ENOINODEBLKS 17

#endif // _ERROR_H_