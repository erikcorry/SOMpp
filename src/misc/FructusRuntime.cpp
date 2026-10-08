/*
 * What the C++ runtime would bring in on fructus, without malloc.
 *
 * Nothing on fructus allocates with new: the heap and its collector are
 * static, see Heap::InitializeHeap() and SlidingHeap, and the heap is the
 * memory the linker leaves below the program. But every class with a virtual
 * destructor gets a deleting destructor, which references operator delete,
 * and the abstract classes' vtables reference __cxa_pure_virtual. The
 * fructus libc defines all three in new.o, whose operator new and delete use
 * malloc, so defining them here keeps new.o, and with it malloc and its page
 * table in the zero page, out of the link.
 */
#ifdef FRUCTUS

  #include <stddef.h>
  #include <stdlib.h>

// nothing is deleted
void operator delete(void* /*p*/) noexcept {}
void operator delete(void* /*p*/, size_t /*size*/) noexcept {}

extern "C" void __cxa_pure_virtual() {
    abort();
}

#endif
