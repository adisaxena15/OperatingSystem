


/*! @file filesys.c‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief File system interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#include "filesys.h"

#include <stddef.h>

#include "console.h"
#include "error.h"
#include "fsimpl.h"
#include "heap.h"
#include "misc.h"
#include "string.h"
#include "uioimpl.h"

// INTERNAL TYPE DEFINITIONS
//

/**
 * @brief Defines the mountpoints within the root file system.
 */
struct mountpoint {
    struct mountpoint* next;  ///< Next mountpoint in linked list
    struct filesystem* fs;    ///< Filesystem function interface
    char name[];              ///< Path alias for mountpoint
};

// INTERNAL FUNCTION PROTOTYPES
//

static struct filesystem* getfs(const char* mpname);
static int fsopen(struct filesystem* fs, const char* flname, struct uio** uioptr);
static int fscreate(struct filesystem* fs, const char* flname);
static int fsdelete(struct filesystem* fs, const char* flname);
static void fsflush(struct filesystem* fs);

static int fs_open_listing(struct uio** uioptr);
static void fs_listing_close(struct uio* uio);
static long fs_listing_read(struct uio* uio, void* buf, unsigned long bufsz);

static int nullfs_open(struct filesystem* fs, const char* flname, struct uio** uioptr);
static void nullfs_flush(struct filesystem* fs);

// INTERNAL GLOBAL VARIABLES
//

/**
 * @brief listing interface for uio.
 */
struct fs_listing_uio {
    struct uio base;              ///< uio base
    const struct mountpoint* fs;  ///< listing object position
};

static const struct uio_intf fs_listing_uio_intf = {.close = &fs_listing_close,
                                                    .read = &fs_listing_read};

static const struct filesystem nullfs = {.open = &nullfs_open, .flush = &nullfs_flush};

// Linked list of mounted filesystems

static struct mountpoint* mplist;

// EXPORTED GLOBAL VARIABLES
//

char fsmgr_initialized = 0;

// EXTERNAL FUNCTION DEFINITIONS
//

int fsmgr_init(void) {
    fsmgr_initialized = 1;
    return 0;
}

/**
 * @brief Flushes all mounted filesystems
 */
void fsmgr_flushall(void) {
    struct mountpoint* mp;

    for (mp = mplist; mp != NULL; mp = mp->next) fsflush(mp->fs);
}

/**
 * @brief Opens a file or directory and wraps it in a uio object
 * @param mpname mount point name (NULL or empty string for listing all mount points)
 * @param flname file name within the mount point (NULL or empty string for listing all files)
 * @param uioptr pointer to uio struct pointer to be filled in
 * @return 0 if successful, negative error code if error
 */
int open_file(const char* mpname, const char* flname, struct uio** uioptr) {
    struct filesystem* fs;

    trace("%s(%s/%s)", __func__, mpname, flname);

    assert(mpname != NULL || flname == NULL);

    if (mpname == NULL || *mpname == '\0') return fs_open_listing(uioptr);

    fs = getfs(mpname);

    return (fs != NULL) ? fsopen(fs, flname, uioptr) : -ENOENT;
}

/**
 * @brief Create a file in the filesystem specified by the path
 * @param mpname mount point name
 * @param flname file name within the mount point
 * @return 0 on success, negative error code if error
 */
int create_file(const char* mpname, const char* flname) {
    struct filesystem* fs;

    if (mpname == NULL || flname == NULL) return -EINVAL;

    fs = getfs(mpname);

    return (fs != NULL) ? fscreate(fs, flname) : -ENOENT;
}

/**
 * @brief Deletes a file in the filesystem specified by the path
 * @param mpname mount point name
 * @param flname file name within the mount point
 * @return 0 on success, negative error code if error
 */
int delete_file(const char* mpname, const char* flname) {
    struct filesystem* fs;

    if (mpname == NULL || flname == NULL) return -EINVAL;

    fs = getfs(mpname);

    return (fs != NULL) ? fsdelete(fs, flname) : -ENOENT;
}

/**
 * @brief Opens a uio object that lists all mounted filesystems
 * @param uioptr pointer to uio struct pointer to be filled in
 * @return 0 if successful, negative error code if error
 */
int fs_open_listing(struct uio** uioptr) {
    struct fs_listing_uio* ls;

    ls = kcalloc(1, sizeof(*ls));
    ls->fs = mplist;

    // Note: The listing uio_intf is at index DEV_UNDEF in /devfs_uio_intfs/.
    *uioptr = uio_init1(&ls->base, &fs_listing_uio_intf);

    return 0;
}

/**
 * @brief Closes a filesystem listing uio object
 * @param uio pointer to uio object to be closed
 */
void fs_listing_close(struct uio* uio) {
    struct fs_listing_uio* const ls = (struct fs_listing_uio*)uio;
    kfree(ls);
}

/**
 * @brief Reads the next filesystem name into the buffer
 * @param uio pointer to filesystem listing uio object
 * @param buf buffer to read the filesystem name into
 * @param bufsz size of the buffer
 * @return number of bytes read, 0 if no more filesystems
 */
long fs_listing_read(struct uio* uio, void* buf, unsigned long bufsz) {
    struct fs_listing_uio* const ls = (struct fs_listing_uio*)uio;
    size_t len;

    if (ls->fs != NULL) {
        len = strlen(ls->fs->name);
        strncpy(buf, ls->fs->name, bufsz);
        ls->fs = ls->fs->next;
        return (len < bufsz) ? len : bufsz;
    } else
        return 0;
}

/**
 * @brief Mounts the null filesystem at the specified mount point name
 * @param name mount point name
 * @return 0 if successful, negative error code if error
 */
int mount_nullfs(const char* name) { return attach_filesystem(name, (struct filesystem*)&nullfs); }

/**
 * @brief Attaches a filesystem to a mount point name
 * @param mpname mount point name
 * @param fs pointer to filesystem
 * @return 0 if successful, -EEXIST if mount point already exists
 */
int attach_filesystem(const char* mpname, struct filesystem* fs) {
    struct mountpoint** mpptr;
    struct mountpoint* mp;
    size_t namelen;

    mpptr = &mplist;
    while ((mp = *mpptr) != NULL) {
        if (strcmp(mp->name, mpname) == 0) return -EEXIST;
        mpptr = &mp->next;
    }

    namelen = strlen(mpname);
    mp = kmalloc(sizeof(*mp) + namelen + 1);
    memset(mp, 0, sizeof(*mp));

    strncpy(mp->name, mpname, namelen + 1);
    mp->fs = fs;
    *mpptr = mp;

    return 0;
}

// INTERNAL FUNCTION DEFINITIONS
//

/**
 * @brief Finds the filesystem mounted at the specified mount point name
 * @param mpname mount point name
 * @return pointer to filesystem if found, NULL if not found
 */
struct filesystem* getfs(const char* mpname) {
    struct mountpoint* mp;

    for (mp = mplist; mp != NULL; mp = mp->next) {
        if (strcmp(mp->name, mpname) == 0) return mp->fs;
    }

    return NULL;
}

/**
 * @brief Opens a file in the specified filesystem
 * @param fs pointer to filesystem
 * @param flname file name within the filesystem
 * @param uioptr pointer to uio struct pointer to be filled in
 * @return 0 if successful, -ENOTSUP if not supported
 */
int fsopen(struct filesystem* fs, const char* flname, struct uio** uioptr) {
    if (fs->open != NULL)
        return fs->open(fs, flname, uioptr);
    else
        return -ENOTSUP;
}

/**
 * @brief Creates a file in the specified filesystem
 * @param fs pointer to filesystem
 * @param flname file name within the filesystem
 * @return 0 if successful, -ENOTSUP if not supported
 */
int fscreate(struct filesystem* fs, const char* flname) {
    if (fs->create != NULL)
        return fs->create(fs, flname);
    else
        return -ENOTSUP;
}

/**
 * @brief Deletes a file in the specified filesystem
 * @param fs pointer to filesystem
 * @param flname file name within the filesystem
 * @return 0 if successful, -ENOTSUP if not supported
 */
int fsdelete(struct filesystem* fs, const char* flname) {
    if (fs->delete != NULL)
        return fs->delete (fs, flname);
    else
        return -ENOTSUP;
}

/**
 * @brief Flushes the specified filesystem
 * @param fs pointer to filesystem
 */
void fsflush(struct filesystem* fs) {
    if (fs->flush != NULL) fs->flush(fs);
}

/**
 * @brief Opens a file in the null filesystem (always fails)
 * @return -ENOENT (always fails)
 */
int nullfs_open(struct filesystem* fs __attribute__((unused)), const char* flname,
                struct uio** uioptr) {
    return -ENOENT;
}

/**
 * @brief Flushes the null filesystem (does nothing)
 */
void nullfs_flush(struct filesystem* fs __attribute__((unused))) {
    // Does nothing
}

/**
 * @brief Parses a path into mount point name and file name
 * @param path string to be parsed
 * @param mpnameptr pointer to which the mount point name will be stored
 * @param flnameptr pointer to which the file name will be stored
 * @return 0 on success, -EINVAL on invalid arguments.
 */
int parse_path(char* path, char** mpnameptr, char** flnameptr) {
    char* slash;
    // we need to validate the arguments
    if (path == NULL || mpnameptr == NULL || flnameptr == NULL) {
        return -EINVAL;
    }
    // we need to strip leading '/' characters
    while (*path == '/') {
        path++;
    }
    // we need to reject empty path after stripping
    if (*path == '\0') {
        return -EINVAL;
    }   
    // we need to find the first '/' after mount name
    slash = strchr(path, '/');
    // we need to handle bare mount name with no slash: "c" or "dev"
    if (slash == NULL) {
        *mpnameptr = path;
        *flnameptr = path + strlen(path);  // empty string
        return 0;
    }
    // we need to split at the slash
    *slash = '\0';
    *mpnameptr = path;
    *flnameptr = slash + 1;
    // we need to require non-empty mount name
    if ((*mpnameptr)[0] == '\0') {
        *slash = '/';
        return -EINVAL;
    }
    // we need to allow empty filename for directory listings
    return 0;
}
