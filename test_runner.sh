#!/bin/bash

echo "Clay Library Comprehensive Test Runner"

cd ~/clay

mkdir -p tests examples

echo "Locating test files..."
echo ""

echo "Looking for your test files:"
find . -name "decode_test.cpp" -o -name "test_buffer_func.cpp" -o -name "test_buffer.cpp" 2>/dev/null | while read file; do
    echo "Found: $file"
done

echo ""

echo "Organizing test files..."

if [ -f "decode_test.cpp" ]; then
    echo "Moving decode_test.cpp to examples/"
    mv decode_test.cpp examples/
elif [ -f "examples/decode_test.cpp" ]; then
    echo "decode_test.cpp already in examples/"
elif [ -f "tests/decode_test.cpp" ]; then
    echo "Moving decode_test.cpp from tests/ to examples/"
    mv tests/decode_test.cpp examples/
fi

if [ -f "test_buffer_func.cpp" ]; then
    echo "Moving test_buffer_func.cpp to tests/"
    mv test_buffer_func.cpp tests/
elif [ -f "tests/test_buffer_func.cpp" ]; then
    echo "test_buffer_func.cpp already in tests/"
elif [ -f "examples/test_buffer_func.cpp" ]; then
    echo "Moving test_buffer_func.cpp from examples/ to tests/"
    mv examples/test_buffer_func.cpp tests/
fi

if [ -f "test_buffer.cpp" ]; then
    echo "Moving test_buffer.cpp to tests/"
    mv test_buffer.cpp tests/
elif [ -f "tests/test_buffer.cpp" ]; then
    echo "test_buffer.cpp already in tests/"
elif [ -f "examples/test_buffer.cpp" ]; then
    echo "Moving test_buffer.cpp from examples/ to tests/"
    mv examples/test_buffer.cpp tests/
fi

echo ""
echo "Current file organization:"
echo "Examples:"
ls -la examples/*.cpp 2>/dev/null || echo "  No .cpp files in examples/"
echo "Tests:"
ls -la tests/*.cpp 2>/dev/null || echo "  No .cpp files in tests/"

echo ""
echo "4. Updating CMakeLists.txt to include all test files..."

cp CMakeLists.txt CMakeLists.txt.backup

cat > CMakeLists_updated.txt << 'EOF'
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

# Examples section
if(BUILD_EXAMPLES)
    # Basic example
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/basic_example.cpp)
        add_executable(clay_example examples/basic_example.cpp)
        target_link_libraries(clay_example clay)
        target_include_directories(clay_example PRIVATE include)
    endif()
    
    # Debug encoding example
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/debug_encoding.cpp)
        add_executable(clay_debug_encoding examples/debug_encoding.cpp)
        target_link_libraries(clay_debug_encoding clay)
        target_include_directories(clay_debug_encoding PRIVATE include)
    endif()
    
    # Decode test example
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/decode_test.cpp)
        add_executable(clay_decode_test examples/decode_test.cpp)
        target_link_libraries(clay_decode_test clay)
        target_include_directories(clay_decode_test PRIVATE include)
    endif()
    
    # Chunk demo example
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/chunk_demo.cpp)
        add_executable(clay_chunk_demo examples/chunk_demo.cpp)
        target_link_libraries(clay_chunk_demo clay)
        target_include_directories(clay_chunk_demo PRIVATE include)
    endif()
endif()

# Tests section with CTest integration
if(BUILD_TESTS)
    enable_testing()
    
    # Original test_buffer.cpp
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_buffer.cpp)
        add_executable(test_buffer tests/test_buffer.cpp)
        target_link_libraries(test_buffer clay)
        target_include_directories(test_buffer PRIVATE include)
        add_test(NAME buffer_test COMMAND test_buffer)
    endif()
    
    # Your test_buffer_func.cpp
    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/test_buffer_func.cpp)
        add_executable(test_buffer_func tests/test_buffer_func.cpp)
        target_link_libraries(test_buffer_func clay)
        target_include_directories(test_buffer_func PRIVATE include)
        add_test(NAME buffer_functionality_test COMMAND test_buffer_func)
    endif()
    
    # Any additional test files
    file(GLOB TEST_FILES tests/test_*.cpp)
    foreach(TEST_FILE ${TEST_FILES})
        get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
        if(NOT TEST_NAME STREQUAL "test_buffer" AND NOT TEST_NAME STREQUAL "test_buffer_func")
            add_executable(${TEST_NAME} ${TEST_FILE})
            target_link_libraries(${TEST_NAME} clay)
            target_include_directories(${TEST_NAME} PRIVATE include)
            add_test(NAME ${TEST_NAME}_test COMMAND ${TEST_NAME})
        endif()
    endforeach()
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

mv CMakeLists_updated.txt CMakeLists.txt
echo "Updated CMakeLists.txt with comprehensive test support"

echo ""
echo "5. Rebuilding with updated configuration..."

rm -rf build
mkdir build
cd build

cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=ON

if [ $? -eq 0 ]; then
    echo ""
    echo "6. Building all targets..."

    make -j$(nproc)
    
    if [ $? -eq 0 ]; then
        echo ""
        echo "7. Running all tests..."
        export LD_LIBRARY_PATH=.:../deps/jerasure/src/.libs:../deps/gf-complete/src/.libs:$LD_LIBRARY_PATH

        echo ""
        echo "Built executables:"
        ls -la clay* test_* 2>/dev/null
        
        echo ""
        echo "Running Tests"

        echo ""
        echo "1. Running test_buffer..."
        if [ -f "test_buffer" ]; then
            ./test_buffer
            echo "test_buffer exit code: $?"
        else
            echo "test_buffer not found"
        fi
        
        echo ""
        echo "2. Running test_buffer_func..."
        if [ -f "test_buffer_func" ]; then
            ./test_buffer_func
            echo "test_buffer_func exit code: $?"
        else
            echo "test_buffer_func not found"
        fi
        
        echo ""
        echo "Running Examples"
        
        echo ""
        echo "3. Running clay_decode_test..."
        if [ -f "clay_decode_test" ]; then
            ./clay_decode_test
            echo "clay_decode_test exit code: $?"
        else
            echo "clay_decode_test not found"
        fi
        
        echo ""
        echo "4. Running clay_debug_encoding..."
        if [ -f "clay_debug_encoding" ]; then
            echo "Running debug encoding (abbreviated output)..."
            ./clay_debug_encoding | grep -E "(Step [0-9]|SUCCESS|FAILED|All zeros|Found non-zero)"
            echo "clay_debug_encoding exit code: $?"
        else
            echo "clay_debug_encoding not found"
        fi
        
        echo ""
        echo "5. Running other examples..."
        for exe in clay_example clay_chunk_demo; do
            if [ -f "$exe" ]; then
                echo "Running $exe..."
                ./$exe
                echo "$exe exit code: $?"
                echo ""
            fi
        done
        
        echo ""
        echo "Running CTest"
        
        if command -v ctest &> /dev/null; then
            echo "Running ctest..."
            ctest --output-on-failure
            echo "ctest exit code: $?"
        else
            echo "ctest not available"
        fi
        
        echo ""
        echo "Test Summary"
        echo "All requested tests have been executed."
        echo "Check the output above for any failures."
        echo ""
        echo "Your test files:"
        echo "- test_buffer.cpp -> test_buffer executable"
        echo "- test_buffer_func.cpp -> test_buffer_func executable"  
        echo "- decode_test.cpp -> clay_decode_test executable"
        echo ""
        echo "All are now properly integrated into the build system!"
        
    else
        echo "Build failed!"
        echo "Check the compilation errors above."
        exit 1
    fi
else
    echo "CMake configuration failed!"
    echo "Check the configuration errors above."
    exit 1
fi