/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

// This file is used to ocnfigure the kernel at compile time

#pragma once

/*******************************************************************************
 *                                  CONSTANTS
 * ****************************************************************************/

// Log levels (absolute, use == in #if)
#define CONST_LOG_LEVEL_OFF 0 /* Disable all logging */
#define CONST_LOG_LEVEL_TTY 1 /* Logs the foregrund tty output */

// Log levels (relative, use >= in #if)
#define CONST_LOG_LEVEL_URGENT  2 /* Enable KURGENT */
#define CONST_LOG_LEVEL_INFO    3 /* Enable KLOG */
#define CONST_LOG_LEVEL_OPTION  4 /* Enable KOPTMSG */
#define CONST_LOG_LEVEL_DEBUG   5 /* Enable KDEBUG */
#define CONST_LOG_LEVEL_USER    6 /* Enable kprint syscall */
#define CONST_LOG_LEVEL_SYSCALL 7 /* Enable system call logging */

// Log location
#define CONST_LOG_LOCATION_NONE 0
#define CONST_LOG_LOCATION_ALL  1

// Syscall log modes
#define CONST_LOG_SYSCALL_BEFORE 0 /* Log syscalls before jumping to them */
#define CONST_LOG_SYSCALL_AFTER  1 /* Log syscalls on return (for ret value) */

// Assert action
#define CONST_ASSERT_REMOVE 0 /* Remove all instances of KASSERT */
#define CONST_ASSERT_EXPAND 1 /* Replace with expression (keep sideeffects) */
#define CONST_ASSERT_SOFT   2 /* Only print warning on assertion fail */
#define CONST_ASSERT_PANIC  3 /* Panic on assertion fail */

// PMM zero policies
#define CONST_PMM_ZERO_ON_ALLOC 0 /* Pages are zeroed on allocation */
#define CONST_PMM_ZERO_ON_FREE  1 /* Pages are zeroed on boot and on free */
#define CONST_PMM_ZERO_OFF      2 /* Pages are not zeroed by PMM */

// Callout modes
#define CONST_CALLOUT_ONLY_BSP 0
#define CONST_CALLOUT_PER_CPU  1

// Mutex modes
#define CONST_MUTEX_SPINLOCK 0
#define CONST_MUTEX_REAL     1

/*******************************************************************************
 *                                CONFIGURATION
 * ****************************************************************************/

#define CONFIG_LOG_LEVEL          CONST_LOG_LEVEL_OPTION /* Kernel log level */
#define CONFIG_LOG_ALLOW_COLORS   1 /* Allow colors in logging */
#define CONFIG_LOG_SEP_LEN        7 /* Length of the separator spaces in logs */
#define CONFIG_LOG_USE_SERIAL     1 /* Use serial output for kernel logging */
#define CONFIG_LOG_USE_VNODE      1 /* Also use a vnode for kernel logging */
#define CONFIG_LOG_USE_SCREEN     1
#define CONFIG_LOG_SHOW_SPLASH    1
#define CONFIG_LOG_SYSCALL_MODE   CONST_LOG_SYSCALL_BEFORE
#define CONFIG_LOG_LOCATION       CONST_LOG_LOCATION_ALL
#define CONFIG_ASSERT_ACTION      CONST_ASSERT_PANIC /* Action taken by KASSERT */
#define CONFIG_OPEN_MAX           96     /* Maximum number of FDs per process */
#define CONFIG_PROC_MAX           250000 /* Maximum number of processes */
#define CONFIG_SYSCALL_MAX        128 /* Maximum number of syscall handlers */
#define CONFIG_SYMLINK_MAX        32
#define CONFIG_PATH_MAX           256
#define CONFIG_TTY_MAX            7
#define CONFIG_PMM_ZERO           CONST_PMM_ZERO_ON_FREE /* PMM page zeroeing policy */
#define CONFIG_PMM_RSVRANGE_MAX   32
#define CONFIG_TERM_FPS           60 /* FPS of buffered terminals */
#define CONFIG_TERM_PANIC         1 /* Display kernel panic to fallback terminal */
#define CONFIG_INIT_PATH          "/boot/init" /* Path to init executable */
#define CONFIG_INIT_ARGV          NULL         /* Init program argv */
#define CONFIG_INIT_ENV           NULL         /* Init program env */
#define CONFIG_CALLOUT_MODE       CONST_CALLOUT_ONLY_BSP
#define CONFIG_MUTEX_MODE         CONST_MUTEX_REAL
#define CONFIG_UNIX_SOCK_RB_SIZE  (256UL * 1024UL)
#define CONFIG_VMM_ANON_START     0x100000000
#define CONFIG_DEFAULT_KBD_LAYOUT en_us
