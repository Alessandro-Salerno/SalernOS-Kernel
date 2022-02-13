#include "Memory/bmp.h"


bool_t kernel_bmp_get(bmp_t* __bmp, uint64_t __idx) {
    uint64_t _byte_idx    = __idx / 8;
    uint8_t  _bit_idx     = __idx % 8;
    uint8_t  _bit_indexer = 0b10000000 >> _bit_idx;

    return (__bmp->_Buffer[_byte_idx] & _bit_indexer) > 0;
}

void kernel_bmp_set(bmp_t* __bmp, uint64_t __idx, bool_t __val) {
    uint64_t _byte_idx    = __idx / 8;
    uint8_t  _bit_idx     = __idx % 8;
    uint8_t  _bit_indexer = 0b10000000 >> _bit_idx;
    
    __bmp->_Buffer[_byte_idx] &= ~_bit_indexer;
    __bmp->_Buffer[_byte_idx] |=  _bit_indexer * __val;
}
