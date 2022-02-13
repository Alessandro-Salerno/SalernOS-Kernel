#include "User/Output/Text/cstr.h"


static char uint_output[128];
const char* kernel_uint_to_str(uint64_t __val) {
    uint64_t _size_test = 10;
    uint8_t  _size      = 0;
    uint8_t  _idx       = 0;

    while (__val / _size_test > 0) {
        _size_test *= 10;
        _size++;
    }

    while (_idx < _size) {
        uint8_t _rem              = __val % 10;
        __val                    /= 10;
        uint_output[_size - _idx] = _rem + '0';
        _idx++;
    }

    uint8_t _rem              = __val % 10;
    __val                    /= 10;
    uint_output[_size - _idx] = _rem + '0';
    uint_output[_size + 1]    = 0;
    
    return uint_output;
}

static char int_output[128];
const char* kernel_int_to_str(int64_t __val) {
    uint64_t _size_test   = 10;
    uint8_t  _size        = 0;
    uint8_t  _idx         = 0;
    uint8_t  _is_negative = 0;

    if (__val < 0) {
        _is_negative = 1;
        __val *= -1;
        int_output[0] = '-';
    }

    while (__val / _size_test > 0) {
        _size_test *= 10;
        _size++;
    }

    while (_idx < _size) {
        uint8_t _rem                             = __val % 10;
        __val                                   /= 10;
        int_output[_is_negative + _size - _idx]  = _rem + '0';
        _idx++;
    }

    uint8_t _rem                             = __val % 10;
    __val                                   /= 10;
    int_output[_is_negative + _size - _idx] = _rem + '0';
    int_output[_is_negative + _size + 1]    = 0;
    
    return int_output;
}
