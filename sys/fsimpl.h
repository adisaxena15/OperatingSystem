/*! @file fsimpl.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief File system implementers' interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _FSIMPL_H_
#define _FSIMPL_H_

struct uio;

/**
 * @brief The /filesystem/ struct defines the backing operations
 * of the filesystem. Use the functions /open/, /flush/, etc.
 * defined below to interact with files within the filesystem.
 */
struct filesystem {
    /**
     * @brief Opens a file within a filesystem
     * @param fs Filesystem containing the file
     * @param name Name of file to open
     * @param uioptr uio object to open file on
     */
    int (*open)(struct filesystem* fs, const char* name, struct uio** uioptr);

    /**
     * @brief Creates a file within a filesystem
     * @param fs Filesystem to create file at
     * @param name Name of file to create
     */
    int (*create)(struct filesystem* fs, const char* name);

    /**
     * @brief Removes a file within a filesystem
     * @param fs Filesystem in which file will be delted
     * @param name Name of file to delete
     */
    int (*delete)(struct filesystem* fs, const char* name);

    /**
     * @brief Flushes the filesystem
     * @param fs Filesystem to flush
     */
    void (*flush)(struct filesystem* fs);
};

extern int attach_filesystem(const char* name, struct filesystem* fs);

#endif  // _FSIMPL_H_
