#!/bin/bash

echo "Building dependencies..."

cd ~/clay

echo "Rebuilding Jerasure..."
cd deps/jerasure

make clean 2>/dev/null || true
rm -f configure Makefile.in aclocal.m4

if [ ! -f "autogen.sh" ] && [ ! -f "configure.ac" ]; then
    echo "Missing Jerasure source files"
    exit 1
fi

echo "Regenerating autotools files..."
autoreconf -fiv

if [ $? -ne 0 ]; then
    echo "autoreconf failed"
    exit 1
fi

echo "Configuring Jerasure..."
./configure --prefix=$(pwd)/install --enable-shared --enable-static

if [ $? -ne 0 ]; then
    echo "Jerasure configure failed"
    exit 1
fi

echo "Building Jerasure..."
make -j$(nproc)

if [ $? -ne 0 ]; then
    echo "Jerasure make failed"
    exit 1
fi

make install

if [ ! -f "src/.libs/libJerasure.a" ] && [ ! -f "src/.libs/libjerasure.a" ]; then
    echo "Jerasure library not found after build"
    ls -la src/.libs/
    exit 1
fi

echo "Jerasure built successfully"

cd ../..

echo ""
echo "Checking gf-complete..."
cd deps/gf-complete

if [ ! -f "src/.libs/libgf_complete.a" ]; then
    echo "Building gf-complete..."
    make clean 2>/dev/null || true
    
    if [ ! -f "configure" ]; then
        ./autogen.sh
    fi
    
    ./configure --prefix=$(pwd)/install --enable-shared --enable-static
    make -j$(nproc)
    make install
    
    if [ ! -f "src/.libs/libgf_complete.a" ]; then
        echo "gf-complete build failed"
        exit 1
    fi
fi

echo "gf-complete is ready"

cd ../..

echo ""
echo "Updating CMakeLists.txt..."

cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.12)
project(libclay VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test programs" ON)

# Find dependencies with better error handling
find_package(PkgConfig REQUIRED)

# Try to find installed libraries first
pkg_check_modules(JERASURE jerasure)
pkg_check_modules(GFCOMPLETE gf-complete)

# Use bundled versions if not found
if(NOT JERASURE_FOUND OR NOT GFCOMPLETE_FOUND)
    message(STATUS "Using bundled jerasure and gf-complete")
    
    # Set include directories
    set(GFCOMPLETE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/deps/gf-complete/include)
    set(JERASURE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/deps/jerasure/include)
    
    # Find the actual library files with multiple possible names
    find_library(GFCOMPLETE_LIBRARIES 
        NAMES gf_complete libgf_complete
        PATHS 
            ${CMAKE_SOURCE_DIR}/deps/gf-complete/src/.libs
            ${CMAKE_SOURCE_DIR}/deps/gf-complete/install/lib
        NO_DEFAULT_PATH
        REQUIRED)
        
    find_library(JERASURE_LIBRARIES 
        NAMES jerasure Jerasure libjerasure libJerasure
        PATHS 
            ${CMAKE_SOURCE_DIR}/deps/jerasure/src/.libs
            ${CMAKE_SOURCE_DIR}/deps/jerasure/install/lib
        NO_DEFAULT_PATH
        REQUIRED)
        
    if(NOT GFCOMPLETE_LIBRARIES)
        message(FATAL_ERROR "Could not find gf-complete library. Please build it first.")
    endif()
    
    if(NOT JERASURE_LIBRARIES)
        message(FATAL_ERROR "Could not find jerasure library. Please build it first.")
    endif()
    
    message(STATUS "Found gf-complete: ${GFCOMPLETE_LIBRARIES}")
    message(STATUS "Found jerasure: ${JERASURE_LIBRARIES}")
endif()

# Library sources
set(CLAY_SOURCES
    src/Buffer.cpp
    src/BufferListAdapter.cpp
    src/ClayParams.cpp
    src/ErasureCode.cpp
    src/ErasureCodeClay.cpp
    src/ErasureCodeJerasure.cpp
    src/ClayCode.cpp
)

set(CLAY_HEADERS
    include/clay/clay.h
    include/clay/Buffer.h
    include/clay/BufferList.h
    include/clay/ErasureCode.h
    include/clay/ErasureCodeClay.h
    include/clay/ErasureCodeInterface.h
    include/clay/ErasureCodeJerasure.h
    include/clay/ErasureCodeProfile.h
)

# Create the main library
add_library(clay ${CLAY_SOURCES})

target_include_directories(clay
    PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${JERASURE_INCLUDE_DIRS}
        ${GFCOMPLETE_INCLUDE_DIRS}
)

target_link_libraries(clay
    PRIVATE
        ${JERASURE_LIBRARIES}
        ${GFCOMPLETE_LIBRARIES}
)

# Set library properties
set_target_properties(clay PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${CLAY_HEADERS}"
)

# Examples
if(BUILD_EXAMPLES)
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/basic_example.cpp)
        add_executable(clay_example examples/basic_example.cpp)
        target_link_libraries(clay_example clay)
        target_include_directories(clay_example PRIVATE include)
    endif()
endif()

# Tests
if(BUILD_TESTS)
    enable_testing()
    
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_buffer.cpp)
        add_executable(test_buffer tests/test_buffer.cpp)
        target_link_libraries(test_buffer clay)
        target_include_directories(test_buffer PRIVATE include)
        add_test(NAME buffer_test COMMAND test_buffer)
    endif()
endif()

# Install rules
include(GNUInstallDirs)

install(TARGETS clay
    EXPORT clayTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/clay
)

install(EXPORT clayTargets
    FILE clayTargets.cmake
    NAMESPACE clay::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clay
)

# Print configuration summary
message(STATUS "")
message(STATUS "Clay Library Configuration Summary:")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "Build examples: ${BUILD_EXAMPLES}")
message(STATUS "Build tests: ${BUILD_TESTS}")
message(STATUS "")
message(STATUS "Dependencies:")
if(JERASURE_FOUND)
    message(STATUS "  Jerasure: Found (system)")
else()
    message(STATUS "  Jerasure: Bundled (${JERASURE_LIBRARIES})")
endif()
if(GFCOMPLETE_FOUND)
    message(STATUS "  GF-Complete: Found (system)")
else()
    message(STATUS "  GF-Complete: Bundled (${GFCOMPLETE_LIBRARIES})")
endif()
message(STATUS "")
EOF

echo "Updated CMakeLists.txt"

echo ""
echo "Testing CMake configuration..."
rm -rf build
mkdir build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release

if [ $? -eq 0 ]; then
    echo "CMake configuration successful!"
    
    echo ""
    echo "Testing build..."
    make -j$(nproc)
    
    if [ $? -eq 0 ]; then
        echo "Build successful!"
        echo ""
        echo "DEPENDENCIES AND BUILD SYSTEM FIXED!"
        echo "Ready to continue with the full test..."
    else
        echo "Build failed"
        echo "Check the error messages above for details"
        exit 1
    fi
else
    echo "CMake configuration still failing"
    echo "Check the error messages above for details"
    exit 1
fi