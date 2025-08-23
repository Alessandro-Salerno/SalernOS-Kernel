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

// Log levels
#define CONST_LOG_LEVEL_OFF     0 /* Disable all logging */
#define CONST_LOG_LEVEL_URGENT  1 /* Enable KURGENT */
#define CONST_LOG_LEVEL_INFO    2 /* Enable KLOG */
#define CONST_LOG_LEVEL_DEBUG   3 /* Enable KDEBUG */
#define CONST_LOG_LEVEL_SYSCALL 4 /* Enable system call logging */

// Assert action
#define CONST_ASSERT_REMOVE 0 /* Remove all instances of KASSERT */
#define CONST_ASSERT_EXPAND 1 /* Replace with expression (keep sideeffects) */
#define CONST_ASSERT_SOFT   2 /* Only print warning on assertion fail */
#define CONST_ASSERT_PANIC  3 /* Panic on assertion fail */

// PMM zero policies
#define CONST_PMM_ZERO_ON_ALLOC 0 /* Pages are zeroed on allocation */
#define CONST_PMM_ZERO_ON_FREE  1 /* Pages are zeroed on boot and on free */
#define CONST_PMM_ZERO_OFF      2 /* Pages are not zeroed by PMM */

/*******************************************************************************
 *                                CONFIGURATION
 * ****************************************************************************/

#define CONFIG_LOG_LEVEL     CONST_LOG_LEVEL_DEBUG /* Kernel log level */
#define CONFIG_ASSERT_ACTION CONST_ASSERT_PANIC    /* Action taken by KASSERT */
#define CONFIG_OPEN_MAX      96   /* Maximum number of FDs per process */
#define CONFIG_PROC_MAX      1024 /* Maximum number of processes */
#define CONFIG_PMM_ZERO      CONST_PMM_ZERO_ON_FREE /* PMM page zeroeing policy */
