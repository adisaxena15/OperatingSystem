/*! @file see.h‌‌‍‍‌‍⁠‌‌​‌‌‌⁠‍‌‌​⁠‍‌‌‌‍​⁠‍‌‌‍⁠​‌‌‍‌​⁠​‍‌‌‌‌‌⁠‍‍‌​⁠⁠‌‌‌​‌​‌‍‌‍‌‍‌‌‍‍​⁠​⁠‌​‍‍‌⁠‌‍‌‍‌​‌‌‍​‌​​‍‌‍‌‍‌​⁠‌‌‍‍‍‌​‍‌​⁠⁠‌
    @brief Supervisor Execution Environment
    @copyright Copyright (c) 2024-2025 University of Illinois
    @license SPDX-License-identifier: NCSA
*/

#ifndef _SEE_H_
#define _SEE_H_

#include <stdint.h>

/*!
 * @brief Halts the system indicating a successful execution
 * @return None
 */
extern void halt_success(void) __attribute__((noreturn));

/*!
 * @brief Halts the system indicating a failure
 * @return None
 */
extern void halt_failure(void) __attribute__((noreturn));

/*!
 * @brief Transfers control to machine mode and then sets the value of the mtimecmp register
 * @param stcmp_value value to set the mtimecmp register to
 * @return None
 */
extern void set_stcmp(uint64_t stcmp_value);

#endif  // _SEE_H_