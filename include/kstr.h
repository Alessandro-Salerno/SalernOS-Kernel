#ifndef SALERNOS_INC_KERNEL_STR
#define SALERNOS_INC_KERNEL_STR

    #include <kerntypes.h>
    #include <kmath.h>


    static char uintOutput[24];
    static const char* uitoa(uint64_t __val) {
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
            uintOutput[_size - _idx] = _rem + '0';
            _idx++;
        }

        uint8_t _rem              = __val % 10;
        __val                    /= 10;
        uintOutput[_size - _idx] = _rem + '0';
        uintOutput[_size + 1]    = 0;
        
        return uintOutput;
    }

    static char intOutput[24];
    static const char* itoa(int64_t __val) {
        uint64_t _size_test   = 10;
        uint8_t  _size        = 0;
        uint8_t  _idx         = 0;
        uint8_t  _is_negative = 0;

        _is_negative = __val < 0;
        intOutput[0] = (__val < 0) ? '-' : '+';
        __val        = kabs(__val);

        while (__val / _size_test > 0) {
            _size_test *= 10;
            _size++;
        }

        while (_idx < _size) {
            uint8_t _rem                             = __val % 10;
            __val                                   /= 10;
            intOutput[_is_negative + _size - _idx]  = _rem + '0';
            _idx++;
        }

        uint8_t _rem                             = __val % 10;
        __val                                   /= 10;
        intOutput[_is_negative + _size - _idx] = _rem + '0';
        intOutput[_is_negative + _size + 1]    = 0;
        
        return intOutput;
    }

#endif
