#pragma once

#include <cstddef>

namespace align {
inline void
align(size_t alignment, size_t size, void*& ptr, size_t space)
{
    if (size <= space) {
        char* p = reinterpret_cast<char*>(
          ~(alignment - 1) &
          (reinterpret_cast<size_t>(ptr) + alignment - 1));
        size_t n = p - static_cast<char*>(ptr);
        if (n <= space - size) {
            ptr = p;
            return p;
        }
    }
    return 0;
}
}