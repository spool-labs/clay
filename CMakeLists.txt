cmake_minimum_required(VERSION 3.12)
project(libclay VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(BUILD_EXAMPLES "Build example programs" ON)
option(BUILD_TESTS "Build test programs" ON)
option(BUILD_CLI "Build the Clay CLI tool" ON)

find_package(PkgConfig REQUIRED)

pkg_check_modules(JERASURE jerasure)
pkg_check_modules(GFCOMPLETE gf-complete)

if(NOT JERASURE_FOUND OR NOT GFCOMPLETE_FOUND)
    message(STATUS "Using bundled jerasure and gf-complete")

    set(GFCOMPLETE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/deps/gf-complete/include)
    set(JERASURE_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/deps/jerasure/include)

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

set(CLAY_SOURCES
    src/ErasureCode.cc
    src/ErasureCodeClay.cc
    src/ErasureCodeJerasure.cc
)

set(CLAY_HEADERS
    include/BufferList.h
    include/ErasureCode.h
    include/ErasureCodeClay.h
    include/ErasureCodeInterface.h
    include/ErasureCodeJerasure.h
    include/ErasureCodeProfile.h
)

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

set_target_properties(clay PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 1
    PUBLIC_HEADER "${CLAY_HEADERS}"
)

if(BUILD_CLI)
    add_executable(clay_cli src/main.cpp)
    target_link_libraries(clay_cli clay)
    target_include_directories(clay_cli PRIVATE include)

    install(TARGETS clay_cli
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
endif()

if(BUILD_EXAMPLES)
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/lib_integration.cpp)
        add_executable(clay_integration examples/lib_integration.cpp)
        target_link_libraries(clay_integration clay)
        target_include_directories(clay_integration PRIVATE include)
    endif()

    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/adv_demos.cpp)
        add_executable(clay_advanced examples/adv_demos.cpp)
        target_link_libraries(clay_advanced clay)
        target_include_directories(clay_advanced PRIVATE include)
    endif()
endif()

if(BUILD_TESTS)
    enable_testing()

    if(BUILD_CLI)
        add_test(NAME cli_builds_test COMMAND clay_cli)
        set_tests_properties(cli_builds_test PROPERTIES 
            WILL_FAIL TRUE 
        )
    endif()

    if(EXISTS ${CMAKE_SOURCE_DIR}/tests/api_test.cpp)
        add_executable(api_test tests/api_test.cpp)
        target_link_libraries(api_test clay)
        target_include_directories(api_test PRIVATE include)
        add_test(NAME clay_api_test COMMAND api_test)
    endif()
endif()

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

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clayConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/clayConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clay
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/clayConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/clayConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/clayConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/clay
)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clay.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/clay.pc"
    @ONLY
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/clay.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

message(STATUS "")
message(STATUS "Clay Library Configuration Summary:")
message(STATUS "Version: ${PROJECT_VERSION}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Shared libraries: ${BUILD_SHARED_LIBS}")
message(STATUS "Build CLI tool: ${BUILD_CLI}")
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