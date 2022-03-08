[![Contributors][contributors-shield]][contributors-url]
[![Forks][forks-shield]][forks-url]
[![Stargazers][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]
[![MIT License][license-shield]][license-url]


## SalernOS Kernel
The SalernOS Kernel is a fundamental component of SalernOS: a toy OS I'm working on.

## Source Tree Strucutre
The source tree is structured as follows:
* `src` Contains all the source code for the core parts of the kernel
* `include` Contains "libraries" ahd headers that are mostly independent from other kernel components (`kerninc.h` is an exception)
* `kstd` Contains a pseudo C tandard Library for the kernel
* `klib` Contains code that is dependent on other parts of the kernel (Such as `kstd`, `include` and `src`), but are not important enough to keep inside the core source
* `make` Contains files included by the main `Makefile`
* `obj` Is created by the `Makefile` and contains the object files (`.o`) derived from the compilation of the kernel
* `bin` Is created by the `Makefile` and contains the final kernel ELF64 binary

Finally, `kernel.ld` is the Linker Script for the kernel.

## Setup
To set the environment up, just type `make setup` and hit enter.

## Building the Kernel
To compile the entire kernel, just type `make` and hit enter. If you want, you can use the SalernOS-Buildenv.

## Source Tree Naming Conventions
* Folders in the main directory must be named using short series of lowercase characters
* Subfolders must be named using `CamelCase` notation and may not exceed 1 word
* File names must **NOT** contain spaces or other special characters, and shall only consist of short series of lowercase characters

These rules do not apply to special files and folders such as Makefiles.

## C Source File Structure (For `src/`)
* Source Files must contain commented notices, such as licenses, warnings and comments as their first sections.
* Source Files must contain Compiler Directives in order:
  * Include statements (From longest to shortest)
  * Macros and constants (Horizontaly aligned)
* Source Files can contain optional sections in the following order:
  * Type definitions
  * Static global variables (Horizontaly aligned)
  * Other global variables (Horizontaly aligned)
  * Static functions
  * Function implementations

Sections must be separated by two blank lines and the file must end with an empty line as well.

### Source File Example
```c
// LICENSE
// WARNING
// DESCRIPTION


#include <kerninc.h>
#include "other.h"


static bool_t   aStaticBool;
static uint32_t aStaticUint32;

bool_t   aBool;
uint32_t aUint32;


static void __static_function__() {
    return;
}


uint32_t kernel_other_implementation() {
    return 50;
}
```

## C Header File Structure (For all subdirectories)
* Header Files must contain commented notices, such as licenses, warnings and comments as their first sections.
* Header Files must contain Compiler Directives in order:
  * Include guards
  * Include statements (From longest to shortest)
  * Macros and constants (Horizontaly aligned)
* Header Files can contain optional sections in the following order:
  * Type definitions
  * `extern` variables (Part of a table of variables comprised of two columns: type and name - Horizontaly and verticaly aligned)
  * Function declarations (Part of a table of functions comprised of three columns: return type, name and arguments - Horizontaly and verticaly aligned)
* Header Files must **NOT** contain:
  * Non `extern` global variables
  * Static functions
* The body of Header Files must be indented

### Header File Example
```c
#ifndef SALERNOS_CORE_KERNEL_HEADERFILE
#define SALERNOS_CORE_KERNEL_HEADERFILE

    #include <kerntypes.h>


    /*************************
           TYPE   NAME
    **************************/
    extern bool_t anExternBool;


    /*********************************************
    RET TYPE  FUNCTION NAME             ARGUMENTS
    *********************************************/
    void      kernel_other_declaration  ();
  
#endif
```

The variables and functions tables must be organized as follows:
* The top border must start with `/` and end with `*` in the precise column where the bottom border ends
* The bottom border must start with `*` and end with `/`. If the header exceeds the longest element's length (Like in the function table example), the `/` must be one column to the right of the header. Otherwise, the `/` must be on the same column as the semicolon of the longest element

## Assembly File Structure
A structure for `.asm` files will soon be defined. In the meanwhile, try to match the style of existing files.

## Naming Conventions (For `src/`)
* Type names must follow the POSIX Standard type notation (`name_t`) and should be as short as possible
* Function names must follow the scheme `kernel_category_target_action`, such as `kernel_kdd_pxcolor_set` and `kernel_kdd_pxcolor_get`. If the category is the target, such as `kernel_mmap_initialize`, the `target` section can be dropped
* Static function names must follow the scheme `__action_target__`, such as `__get_pxptr__`
* Static global variables and global variables must usa `pascalCase` notation
* Struct member variables must use `CamelCase` notation and start with `_`

## Best Practices
* Use 4-space indentation
* Use `{` on the same line as the declaration
* Avoid `if` statements when possible
* Try to use case-specific types (Such as `uint16_t`) instead of generic types like `int`
* Try to leave spaces inside scopes to separate code areas
* Try to be as precise as possible with alignment and spaces. Any request containing `(){` or `a=b` instead of `() {` and `a = b` **WILL BE REJECTED WITHOUT FURTHER INSPECTION**

## Contributing
For you Pull Request to be accepted, you must follow **ALL** the above mentioned conventions and standards.
You must also describe the changes you made in a simple and organized way, otherwise your request may take longer than usual to process and is more likelly to be rejected.
Expect to wait days or weeks for your request to be processed.
