#include <kstr.h>


static char kstrOutput[24];


const char* uitoa(uint64_t __val) {
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
        kstrOutput[_size - _idx] = _rem + '0';
        _idx++;
    }

    uint8_t _rem              = __val % 10;
    __val                    /= 10;
    kstrOutput[_size - _idx] = _rem + '0';
    kstrOutput[_size + 1]    = 0;
    
    return kstrOutput;
}

const char* itoa(int64_t __val) {
    uint64_t _size_test   = 10;
    uint8_t  _size        = 0;
    uint8_t  _idx         = 0;
    uint8_t  _is_negative = 0;

    _is_negative = __val < 0;
    kstrOutput[0] = (__val < 0) ? '-' : '+';
    __val        = kabs(__val);

    while (__val / _size_test > 0) {
        _size_test *= 10;
        _size++;
    }

    while (_idx < _size) {
        uint8_t _rem                             = __val % 10;
        __val                                   /= 10;
        kstrOutput[_is_negative + _size - _idx]  = _rem + '0';
        _idx++;
    }

    uint8_t _rem                             = __val % 10;
    __val                                   /= 10;
    kstrOutput[_is_negative + _size - _idx]  = _rem + '0';
    kstrOutput[_is_negative + _size + 1]     = 0;
    
    return kstrOutput;
}
