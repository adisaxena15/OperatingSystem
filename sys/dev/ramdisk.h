/*! @file ramdisk.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Memory-backed storage interface
    @copyright Copyright (c) 2024-2025 University of Illinois

*/

#ifndef _RAMDISK_H_
#define _RAMDISK_H_

#include <stddef.h>

struct storage;  // forward declaration

// EXPORTED FUNCTION DECLARATIONS
//

extern void ramdisk_attach();

#endif  // _RAMDISK_H_