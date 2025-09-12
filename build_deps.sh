#!/bin/bash

# Build script for Clay erasure code dependencies
set -e

echo "Building Clay erasure code dependencies..."

# Build GF-Complete
echo "Building GF-Complete..."
cd deps/gf-complete
if [ ! -f "configure" ]; then
    autoreconf -fiv
fi
if [ ! -f "Makefile" ]; then
    ./configure --prefix=$(pwd)/install
fi
make -j$(nproc)
make install
cd ../..

# Build Jerasure
echo "Building Jerasure..."
cd deps/jerasure
if [ ! -f "configure" ]; then
    autoreconf -fiv
fi
if [ ! -f "Makefile" ]; then
    ./configure --prefix=$(pwd)/install LDFLAGS="-L$(pwd)/../gf-complete/install/lib" CPPFLAGS="-I$(pwd)/../gf-complete/install/include"
fi
make -j$(nproc)
make install
cd ../..

echo "Dependencies built successfully!"
echo "GF-Complete installed to: deps/gf-complete/install"
echo "Jerasure installed to: deps/jerasure/install"
