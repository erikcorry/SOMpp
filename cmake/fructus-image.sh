#!/bin/sh
# Compile the classes a program needs on the host, and assemble them for
# fructus into one relocatable object, see src/vm/FructusImage.h.
#
#   fructus-image.sh <host SOM++> <fructus as> <fructus ld> <out.o> \
#                    <classpath> <main class file> [arguments...]
set -e

som=$1 as=$2 ld=$3 out=$4 classpath=$5
shift 5

dir=${out%.o}
rm -rf "$dir"
mkdir -p "$dir"

"$som" -cp "$classpath" -asm "$dir" "$@" >/dev/null

for s in "$dir"/*.s; do
    "$as" "$s" -o "${s%.s}.o"
done
"$ld" -r -o "$out" "$dir"/*.o
