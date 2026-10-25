#pragma once

/*
 * A send bytecode carries its own inline cache:
 *
 *   bytecode, numberOfArguments, target, receiverClass
 *
 * target and receiverClass are words, see bytecodes.h. A unary send,
 * SEND_1, has no numberOfArguments, since it's always 1 (the receiver), so
 * its inline cache is a byte earlier and it's a byte shorter. SEND_EQEQ, a
 * send of #==, is laid out like SEND, and is only a send when the interpreter
 * can't answer it itself.
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
 * A static send, STATIC_SEND or STATIC_SEND_1, is just the bytecode and the
 * invokable it calls, whatever the receiver is. Only the fructus image writer
 * makes them, for the sends that can only call one invokable, see
 * FructusImage.h, and they aren't send sites: IsSendBytecode() is false.
 *
 * On fructus a send has no receiverClass, and no selector: the target is
 * always an invokable, which starts out as the first of the ring of the
 * invokables with the selector, see FructusImage.h. A send hits if the
 * receiver's classId is in the target's first range of classIds. Otherwise
 * the interpreter looks for the receiver's classId in the target's other
 * ranges and then in the ranges of the others in the ring, and links the
 * send to the one it finds. A send whose selector no class has is
 * NOT_UNDERSTOOD instead, which is just an error: the bytecode, the number
 * of arguments and the selector.
 */

#include <cstddef>
#include <cstdint>

#include "bytecodes.h"

class VMClass;
class VMInvokable;
class VMSymbol;

namespace SendSite {
constexpr size_t numberOfArguments = 1;

/** Where the target is in a send with bytecode `bc`. */
constexpr size_t Target(uint8_t bc) {
    return bc == BC_SEND_1 || bc == BC_SEND_1_STACK ? 1 : 2;
}

#ifdef FRUCTUS
constexpr size_t Length(uint8_t bc) {
    return Target(bc) + sizeof(void*);
}
#else
constexpr size_t ReceiverClass(uint8_t bc) {
    return Target(bc) + sizeof(void*);
}

constexpr size_t Length(uint8_t bc) {
    return ReceiverClass(bc) + sizeof(void*);
}
#endif

/** Where the target of a static send is, which is all it has. */
constexpr size_t staticTarget = 1;
constexpr size_t staticLength = staticTarget + sizeof(void*);

/** Where the selector of a NOT_UNDERSTOOD is. */
constexpr size_t notUnderstoodSelector = 2;
constexpr size_t notUnderstoodLength = notUnderstoodSelector + sizeof(void*);

/** The longest a send is. */
constexpr size_t maxLength = Length(BC_SEND);

inline uint8_t GetNumberOfArguments(const uint8_t* site) {
    return site[0] == BC_SEND_1 || site[0] == BC_SEND_1_STACK
               ? 1
               : site[numberOfArguments];
}

/**
 * The selector if the send is unlinked, otherwise the invokable, which it
 * always is on fructus. `bc` is the send's bytecode, which the interpreter
 * knows.
 */
inline void* GetTarget(const uint8_t* site, uint8_t bc) {
    return LoadWord(site + Target(bc));
}

inline void* GetTarget(const uint8_t* site) {
    return GetTarget(site, site[0]);
}

#ifdef FRUCTUS
/** Make the send call `invokable`, see above. */
inline void Link(uint8_t* site, VMInvokable* invokable) {
    StoreWord(site + Target(site[0]), invokable);
}
#else
/** Null if the send is unlinked. */
inline VMClass* GetReceiverClass(const uint8_t* site, uint8_t bc) {
    return (VMClass*)LoadWord(site + ReceiverClass(bc));
}

inline VMClass* GetReceiverClass(const uint8_t* site) {
    return GetReceiverClass(site, site[0]);
}

inline void Init(uint8_t* site, uint8_t bytecode, uint8_t numArgs,
                 VMSymbol* selector) {
    site[0] = bytecode;
    if (bytecode != BC_SEND_1) {
        site[numberOfArguments] = numArgs;
    }
    StoreWord(site + Target(bytecode), selector);
    StoreWord(site + ReceiverClass(bytecode), nullptr);
}

/** The caller does the write barrier for the method the site is in. */
inline void Link(uint8_t* site, VMInvokable* invokable,
                 VMClass* receiverClass) {
    StoreWord(site + Target(site[0]), invokable);
    StoreWord(site + ReceiverClass(site[0]), receiverClass);
}
#endif
}  // namespace SendSite
