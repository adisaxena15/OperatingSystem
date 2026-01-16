/*! @file filesys.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief File system interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _FILESYS_H_
#define _FILESYS_H_

#include "cache.h"
#include "fsimpl.h"
#include "uio.h"

extern char fsmgr_initialized;

extern int fsmgr_init(void);
extern void fsmgr_flushall(void);

extern int open_file(const char *mpname, const char *flname, struct uio **uioptr);
extern int create_file(const char *mpname, const char *flname);
extern int delete_file(const char *mpname, const char *flname);

extern int attach_filesystem(const char *name, struct filesystem *fs);

extern int parse_path(char *path, char **mpnameptr, char **flnameptr);

extern int mount_devfs(const char *name);                      // device.c
extern int mount_ktfs(const char *name, struct cache *cache);  // fs/ktfs.c
extern int mount_nullfs(const char *name);                     // fs.c

#endif  // _FILESYS_H_
