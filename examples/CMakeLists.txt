cmake_minimum_required(VERSION 3.10)

# Find the Clay library
find_library(CLAY_LIB clay_ec PATHS ${CMAKE_SOURCE_DIR}/../build)
find_library(JERASURE_LIB Jerasure PATHS ${CMAKE_SOURCE_DIR}/../deps/jerasure/install/lib)
find_library(GF_COMPLETE_LIB gf_complete PATHS ${CMAKE_SOURCE_DIR}/../deps/gf-complete/install/lib)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/../include
    ${CMAKE_SOURCE_DIR}/../deps/jerasure/install/include
    ${CMAKE_SOURCE_DIR}/../deps/gf-complete/install/include
)

# Create the simple example
add_executable(simple_example simple_example.cpp)
target_link_libraries(simple_example ${CLAY_LIB} ${JERASURE_LIB} ${GF_COMPLETE_LIB})
