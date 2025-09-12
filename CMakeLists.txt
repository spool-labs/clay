cmake_minimum_required(VERSION 3.10)
project(ClayErasureCode VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required dependencies
find_library(JERASURE_LIB Jerasure PATHS ${CMAKE_SOURCE_DIR}/deps/jerasure/install/lib)
find_library(GF_COMPLETE_LIB gf_complete PATHS ${CMAKE_SOURCE_DIR}/deps/gf-complete/install/lib)

if(NOT JERASURE_LIB)
    message(FATAL_ERROR "Jerasure library not found. Please build dependencies first.")
endif()

if(NOT GF_COMPLETE_LIB)
    message(FATAL_ERROR "GF-Complete library not found. Please build dependencies first.")
endif()

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/deps/jerasure/install/include
    ${CMAKE_SOURCE_DIR}/deps/gf-complete/install/include
)

# Set library directories for linking
link_directories(
    ${CMAKE_SOURCE_DIR}/deps/jerasure/install/lib
    ${CMAKE_SOURCE_DIR}/deps/gf-complete/install/lib
)

# Source files for the library
set(CLAY_SOURCES
    src/ErasureCode.cc
    src/ErasureCodeClay.cc
    src/ErasureCodeJerasure.cc
)

# Create the Clay library
add_library(clay_ec STATIC ${CLAY_SOURCES})
target_link_libraries(clay_ec ${JERASURE_LIB} ${GF_COMPLETE_LIB})

# Create the test executable
add_executable(clay_test src/main.cpp)
target_link_libraries(clay_test clay_ec)

# Install targets
install(TARGETS clay_ec DESTINATION lib)
install(DIRECTORY include/ DESTINATION include/clay)

# Create pkg-config file
configure_file(
    ${CMAKE_SOURCE_DIR}/clay.pc.in
    ${CMAKE_BINARY_DIR}/clay.pc
    @ONLY
)
install(FILES ${CMAKE_BINARY_DIR}/clay.pc DESTINATION lib/pkgconfig)

# Enable testing
enable_testing()
add_test(NAME clay_basic_test COMMAND clay_test)
