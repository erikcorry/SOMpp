I'm considering if we can separate SOMpp changes into closed-world changes, vs
actual fructus changes.  Closed-world changes make assumptions about the core
libs, don't allow compilation at run time, make do-not-understand fatal, don't
allow non-literal sends, and are able to make a more efficient == bytecode.
They should run on the host in the host-but-closed-world mode  Actual fructus
changes include no floating point, not having hashcode methods on every object,
and the fact that the biggest int type is 64 bit silently wrapping.  In closed
world host mode I hope it's possible to prevent the bignum and wide int
functions from creating heap numbers that could have been Smis, but perhaps
floating point is a little harder.  Does upstream SOM++ allow == comparisons
between floats and ints to work in the expected way?
