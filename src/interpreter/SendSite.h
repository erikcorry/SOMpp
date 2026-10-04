#pragma once

/*
 * A send bytecode carries its own inline cache:
 *
 *   bytecode, numberOfArguments, target, receiverClass
 *
 * target and receiverClass are words at any alignment, so they're read and
 * written with memcpy, which is an unaligned load or store.
 *
 * A send starts out unlinked: the target is the selector and the
 * receiverClass is null. The first time it's executed it's linked, and is
 * monomorphic from then on: the target is the invokable the selector found
 * in the receiver's class, which is the receiverClass, and a send to an
 * instance of that class calls it without a lookup. A send to any other
 * class looks the selector up every time, from the target's signature, and
 * leaves the cache as it is. There are no polymorphic or megamorphic caches.
 *
 * A super send doesn't depend on the receiver: once it's linked, the target
 * is the invokable and the receiverClass is the superclass it was found in.
 *
 * On the host the GC walks the targets and classes, see VMMethod. On
 * fructus they are never moved or freed, so it doesn't.
 */

#include <cstddef>
#include <cstdint>
#include <string.h>

class VMClass;
class VMInvokable;
class VMSymbol;

namespace SendSite {
constexpr size_t numberOfArguments = 1;
constexpr size_t target = 2;
constexpr size_t receiverClass = target + sizeof(void*);
constexpr size_t length = receiverClass + sizeof(void*);

inline void* LoadWord(const uint8_t* p) {
    void* value = nullptr;
    memcpy((void*)&value, p, sizeof(value));
    return value;
}

inline void StoreWord(uint8_t* p, const void* value) {
    memcpy(p, (const void*)&value, sizeof(value));
}

inline uint8_t GetNumberOfArguments(const uint8_t* site) {
    return site[numberOfArguments];
}

/** The selector if the send is unlinked, otherwise the invokable. */
inline void* GetTarget(const uint8_t* site) {
    return LoadWord(site + target);
}

/** Null if the send is unlinked. */
inline VMClass* GetReceiverClass(const uint8_t* site) {
    return (VMClass*)LoadWord(site + receiverClass);
}

inline void Init(uint8_t* site, uint8_t bytecode, uint8_t numArgs,
                 VMSymbol* selector) {
    site[0] = bytecode;
    site[numberOfArguments] = numArgs;
    StoreWord(site + target, selector);
    StoreWord(site + receiverClass, nullptr);
}

/** The caller does the write barrier for the method the site is in. */
inline void Link(uint8_t* site, VMInvokable* invokable,
                 VMClass* receiverClass) {
    StoreWord(site + target, invokable);
    StoreWord(site + SendSite::receiverClass, receiverClass);
}
}  // namespace SendSite
