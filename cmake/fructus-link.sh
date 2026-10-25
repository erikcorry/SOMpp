#!/bin/sh
# Links the fructus program twice, see cmake/fructus-som.ld: once to measure
# its writable and read-only parts, and again to put the read-only part at
# 0x8000, or as high as it fits below the interpreter at 0xf000 if it's
# bigger than 28K, and the writable part right below it, where the SOM heap
# ends.
#
#   fructus-link.sh <nm> <compiler> <arguments of the link...>
#
# The arguments include `-o <file>' and `-T cmake/fructus-som.ld'.
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
    printf '%d' "0x$("$nm" "$output" | awk -v name="$1" '$3 == name { print $1 }')"
}

"$@"
rw_size=$(($(symbol __rw_end) - $(symbol __rw_start)))
ro_size=$(($(symbol __ro_end) - $(symbol __ro_start)))

ro_at=$(((0xf000 - ro_size) & ~15))
if [ $ro_at -gt $((0x8000)) ]; then
    ro_at=$((0x8000))
fi
rw_at=$(((ro_at - rw_size) & ~15))
# The alignment of the sections can make the parts a little bigger where
# they end up.
for attempt in 1 2 3; do
    if "$@" -Wl,--defsym=__ro_at=$(printf '0x%x' $ro_at) \
        -Wl,--defsym=__rw_at=$(printf '0x%x' $rw_at) 2>/dev/null; then
        exit 0
    fi
    if [ $ro_at -lt $((0x8000)) ]; then
        ro_at=$((ro_at - 16))
    fi
    rw_at=$((rw_at - 16))
done
"$@" -Wl,--defsym=__ro_at=$(printf '0x%x' $ro_at) \
    -Wl,--defsym=__rw_at=$(printf '0x%x' $rw_at)
