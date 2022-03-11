#include <kmath.h>


uint64_t kabs(int64_t __val) {
    return (__val < 0) ? __val * -1 : __val;
}
