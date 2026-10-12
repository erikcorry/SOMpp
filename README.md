Fructus SOM++ - The Simple Object Machine implemented in C++ for a 16 bit soft core
===================================================================================

Introduction
------------

SOM is a minimal Smalltalk dialect used to teach VM construction at the [Hasso
Plattner Institute][SOM]. It was originally built at Aarhus University
(Denmark) where it was also used for teaching.

This fork of SOM is optimized for very small devices. The primary platform
is [https://github.com/erikcorry/fructus](Fructus), a CPU that can be
implemented on very small FPGAs. On such small devices, the reflective
features are not available. The Smalltalk compiler stays on the host.

The GC is a two generation sliding compactor with a card marking write
barrier. This GC is not available in upstream SOM++. On this fork it
is available on the host for testing purposes. Because it uses a sliding
compactor in a linear space it is immune to fragmentation, and it never
allocates during GC. All data structures needed for GC (marking stack,
forwarding tables, promotion space) are pre-allocated. This means GC
can be postponed until allocation fails because memory is 100% full,
another difference to upstream.

Dispatch uses inline caches that do not allocate space. This area is
under development.

Upstream README from the SOM++ repository
-----------------------------------------

Implementations of SOM exist for instance for Java (SOM), C (CSOM), C++ (SOM++), Python & RPython
(PySOM), the Truffle framework (TruffleSOM), and Squeak/Pharo Smalltalk (AweSOM).

A simple SOM Hello World looks like:

```Smalltalk
Hello = (
  run = (
    'Hello World!' println.
  )
)
```

This repository contains the Fructus fork of the C++ implementation of SOM. The
implementation of the SOM standard library and a number of examples are
included as a git submodule.  Please see the [main project page][SOMst] for
links to other VM implementations.


Prerequisites
-------------

The SOM standard library and examples are located in the `core-lib` git submodule.
Before building, initialize and fetch the submodules:

```bash
git submodule update --init --recursive
```


Building
--------

SOM++ uses CMake and an optimized release build can be built like this:

```bash
mkdir cmake-build && cd cmake-build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j
```

Afterwards, the tests can be executed with:

```bash
./SOM++ -cp ../Smalltalk ../TestSuite/TestHarness.som
```

A simple Hello World program is executed with:

```bash
./SOM++ -cp ../Smalltalk ../Examples/Hello.som
```

Information on previous authors are included in the AUTHORS file. This code is
distributed under the MIT License. Please see the LICENSE file for details.
Additional documentation, detailing for instance the object model and how to
implement primitives, is available in the `doc` folder.


Advanced Compilation Settings
-----------------------------

SOM++ supports different garbage collectors, including a basic mark/sweep, and
a generational GC. Furthermore, it implements different variants for integer
handling.


Tagged integers:

    default: off
    option name: TAGGING
    example: cmake .. -DUSE_TAGGING=true

Integer caching:

    default: off
    option name: INT_CACHE
    example: cmake .. -DCACHE_INTEGER=true


Development Build and Testing
-----------------------------

A build with assertions for development and debugging can be built with:

```bash
mkdir cmake-debug && cd cmake-debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
make -j
```

This build also creates a `unittests` binary, which uses cppunit for
implementation-level unit tests, to run the basic interpreter tests
in `TestSuite/BasicInterpreterTests`, as well as the SOM test suite.

The unit tests need the SOM classpath set as follows:

```bash
./unittests -cp ../Smalltalk:../TestSuite/BasicInterpreterTests ../Examples/Hello.som
```

These tests can also be run with CMake, either all together with `make test`
or using the `ctest` tool, which allows to run individual tests:

```
make test
ctest
ctest -R unittests
```

Code Style and Linting
----------------------

To have a somewhat consistent code style and catch some basic bugs, the CI
setup runs `clang-format` and `clang-tidy` as part of the CMake setup.

```bash
make check-tidy
make check-format
```

To run the format and fix tidy issues, we have:

```bash
make tidy
make format
```


Build Status
------------

Thanks to GitHub Actions, this repository is automatically tested.
The current build status is: [![Build Status](https://github.com/SOM-st/SOMpp/actions/workflows/ci.yml/badge.svg)](https://github.com/SOM-st/SOMpp/actions/workflows/ci.yml)

 [SOM]: http://www.hpi.uni-potsdam.de/hirschfeld/projects/som/
 [SOMst]: https://som-st.github.io
