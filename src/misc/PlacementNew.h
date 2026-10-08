#pragma once

/** Placement new: fructus's headers have no <new>. */
#ifdef FRUCTUS
  #include <stddef.h>

inline void* operator new(size_t /*size*/, void* place) noexcept {
    return place;
}
#else
  #include <new>
#endif
