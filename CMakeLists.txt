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
