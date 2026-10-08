#!/bin/sh
# Links the fructus program twice, to give the SOM heap all the memory the
# program leaves: once to learn how big the program is, and again with it as
# high as it fits. The heap is what the linker leaves between the stack and
# the program, see src/memory/SlidingHeap.cpp, and its top stays below 32K,
# which the object starts' table covers.
#
#   fructus-link.sh <nm> <compiler> <arguments of the link...>
#
# The arguments include `-o <file>'. The first link puts the program right
# above the stack, where it always fits if it fits at all.
set -e
nm=$1
shift

output=
previous=
for arg in "$@"; do
    if [ "$previous" = "-o" ]; then
        output=$arg
    fi
    previous=$arg
done

symbol() {
    "$nm" "$output" | awk -v name="$1" '$3 == name { print $1 }'
}

"$@" -Wl,--defsym=__heap_end=0x800
start=$((0x$(symbol __heap_end)))
end=$((0x$(symbol __bss_end)))
size=$((end - start))

# the program's end may not reach 64K, and the heap's top is a pagelet boundary
heap_end=$(((0x10000 - size) & ~0x7f))
if [ $heap_end -gt $((0x8000)) ]; then
    heap_end=$((0x8000))
fi
# The size can change with where the program is, if only by a few bytes.
for attempt in 1 2 3; do
    if "$@" -Wl,--defsym=__heap_end=$(printf '0x%x' $heap_end) 2>/dev/null; then
        exit 0
    fi
    heap_end=$((heap_end - 0x80))
done
"$@" -Wl,--defsym=__heap_end=$(printf '0x%x' $heap_end)
