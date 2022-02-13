#include "GDT/gdt.h"


__attribute__((__aligned__(0x1000)))
gdt_t gdt = {
    { 0, 0, 0, 0x00, 0x00, 0 }, // Null Segment
    { 0, 0, 0, 0x9A, 0xA0, 0 }, // Kernel Code Segment
    { 0, 0, 0, 0x92, 0xA0, 0 }, // Kernel Data Segment
    { 0, 0, 0, 0x00, 0x00, 0 }, // User Null Segment
    { 0, 0, 0, 0x9A, 0xA0, 0 }, // User Code Segment
    { 0, 0, 0, 0x92, 0xA0, 0 }, // User Data Segment
};
