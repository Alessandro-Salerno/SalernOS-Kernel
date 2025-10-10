#include <lib/crt.h>
#include <vendor/printf.h>

// Using __VA_ARGS__ cause it's easier and the result is the same
#define uacpi_memcpy(...)  memcpy(__VA_ARGS__)
#define uacpi_memmove(...) memmove(__VA_ARGS__)
#define uacpi_memset(...)  memset(__VA_ARGS__)
#define uacpi_memcmp(...)  memcmp(__VA_ARGS__)
