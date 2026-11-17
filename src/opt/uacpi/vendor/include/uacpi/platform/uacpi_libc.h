#include <kernel/com/crt.h>
#include <vendor/printf.h>

// Builtins inline constant sizes and fall back to the kernel functions
#define uacpi_memcpy(...)  __builtin_memcpy(__VA_ARGS__)
#define uacpi_memmove(...) __builtin_memmove(__VA_ARGS__)
#define uacpi_memset(...)  __builtin_memset(__VA_ARGS__)
#define uacpi_memcmp(...)  __builtin_memcmp(__VA_ARGS__)
