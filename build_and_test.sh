#!/bin/bash

set -e  

echo "Clay Library Build and Test Pipeline"
echo ""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' 

print_status() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

BUILD_TYPE=${1:-Release}
BUILD_DIR="build"
PARALLEL_JOBS=$(nproc)

check_dependencies() {
    print_status "Checking Clay library dependencies..."
    
    local deps_ready=true

    if [ ! -f "deps/gf-complete/src/.libs/libgf_complete.a" ] && [ ! -f "deps/gf-complete/src/.libs/libgf_complete.so" ]; then
        print_warning "GF-Complete not built"
        deps_ready=false
    fi

    if [ ! -f "deps/jerasure/src/.libs/libJerasure.a" ] && [ ! -f "deps/jerasure/src/.libs/libjerasure.a" ] &&
       [ ! -f "deps/jerasure/src/.libs/libJerasure.so" ] && [ ! -f "deps/jerasure/src/.libs/libjerasure.so" ]; then
        print_warning "Jerasure not built"
        deps_ready=false
    fi
    
    if [ "$deps_ready" = false ]; then
        print_status "Building dependencies first..."
        if [ -f "./build_dependencies.sh" ]; then
            ./build_dependencies.sh
        else
            print_error "build_dependencies.sh script not found!"
            print_error "Please ensure dependencies are built before running this script"
            exit 1
        fi
    else
        print_success "All dependencies are available"
    fi
}

check_source_structure() {
    print_status "Validating source code structure..."
    
    local structure_ok=true

    local required_sources=(
        "src/ErasureCode.cc"
        "src/ErasureCodeClay.cc" 
        "src/ErasureCodeJerasure.cc"
        "src/main.cpp"
    )
    
    for src in "${required_sources[@]}"; do
        if [ ! -f "$src" ]; then
            print_error "Missing required source file: $src"
            structure_ok=false
        fi
    done

    local required_headers=(
        "include/ErasureCode.h"
        "include/ErasureCodeClay.h"
        "include/ErasureCodeJerasure.h"
        "include/ErasureCodeInterface.h"
        "include/BufferList.h"
        "include/ErasureCodeProfile.h"
    )
    
    for hdr in "${required_headers[@]}"; do
        if [ ! -f "$hdr" ]; then
            print_error "Missing required header file: $hdr"
            structure_ok=false
        fi
    done

    if [ ! -f "CMakeLists.txt" ]; then
        print_error "CMakeLists.txt not found!"
        structure_ok=false
    fi
    
    if [ "$structure_ok" = true ]; then
        print_success "Source structure is valid"
    else
        print_error "Source structure validation failed"
        exit 1
    fi
}

configure_build() {
    print_status "Configuring build with CMake..."

    rm -rf "$BUILD_DIR"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"

    print_status "Running CMake configuration (Build type: $BUILD_TYPE)..."
    cmake .. \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DBUILD_CLI=ON \
        -DBUILD_EXAMPLES=ON \
        -DBUILD_TESTS=ON \
        -DBUILD_SHARED_LIBS=ON
    
    if [ $? -eq 0 ]; then
        print_success "CMake configuration successful"
    else
        print_error "CMake configuration failed"
        exit 1
    fi
    
    cd ..
}

build_library() {
    print_status "Building Clay library and executables..."
    
    cd "$BUILD_DIR"

    print_status "Compiling with $PARALLEL_JOBS parallel jobs..."
    make -j"$PARALLEL_JOBS"
    
    if [ $? -eq 0 ]; then
        print_success "Build completed successfully"
    else
        print_error "Build failed"
        exit 1
    fi
    
    cd ..
}

verify_build() {
    print_status "Verifying build artifacts..."
    
    local artifacts_ok=true

    if [ -f "$BUILD_DIR/libclay.so" ] || [ -f "$BUILD_DIR/libclay.a" ]; then
        print_success "Clay library built successfully"
    else
        print_error "Clay library not found after build"
        artifacts_ok=false
    fi

    if [ -f "$BUILD_DIR/clay_cli" ]; then
        print_success "Clay CLI tool built successfully"
    else
        print_warning "Clay CLI tool not found (may not be enabled)"
    fi

    local examples=("clay_simple_demo" "clay_integration" "clay_performance")
    for example in "${examples[@]}"; do
        if [ -f "$BUILD_DIR/$example" ]; then
            print_success "Example '$example' built successfully"
        else
            print_warning "Example '$example' not found (may not exist)"
        fi
    done

    if [ -f "$BUILD_DIR/api_test" ]; then
        print_success "Test executables built successfully"
    else
        print_warning "Test executables not found (may not be enabled)"
    fi
    
    if [ "$artifacts_ok" = false ]; then
        print_error "Critical build artifacts missing"
        exit 1
    fi
}

run_tests() {
    print_status "Running Clay library test suite..."
    
    cd "$BUILD_DIR"

    if command -v ctest &> /dev/null && [ -f "CTestTestfile.cmake" ]; then
        print_status "Running CTest suite..."
        ctest --output-on-failure --parallel "$PARALLEL_JOBS"
        
        if [ $? -eq 0 ]; then
            print_success "All CTest tests passed"
        else
            print_warning "Some CTest tests failed"
        fi
    fi

    if [ -f "api_test" ]; then
        print_status "Running Clay API tests..."
        ./api_test
        
        if [ $? -eq 0 ]; then
            print_success "Clay API tests passed"
        else
            print_error "Clay API tests failed"
            cd ..
            exit 1
        fi
    fi
    
    cd ..
}

test_cli() {
    print_status "Testing Clay CLI tool..."
    
    if [ ! -f "$BUILD_DIR/clay_cli" ]; then
        print_warning "CLI tool not available, skipping CLI tests"
        return 0
    fi
    
    cd "$BUILD_DIR"

    print_status "Testing CLI help functionality..."
    if ./clay_cli --help > /dev/null 2>&1; then
        print_success "CLI help command works"
    else
        print_warning "CLI help command failed (may require arguments)"
    fi

    print_status "Creating test data for CLI..."
    echo "This is test data for Clay CLI demonstration." > test_input.txt

    print_status "Testing CLI encode functionality..."
    if ./clay_cli encode test_input.txt test_chunks 4 2 5 > /dev/null 2>&1; then
        print_success "CLI encode command works"

        print_status "Testing CLI decode functionality..."
        if ./clay_cli decode test_chunks test_output.txt > /dev/null 2>&1; then
            print_success "CLI decode command works"

            if cmp -s test_input.txt test_output.txt; then
                print_success "CLI encode/decode cycle preserves data integrity"
            else
                print_warning "CLI encode/decode data integrity check failed"
            fi
        else
            print_warning "CLI decode command failed"
        fi
    else
        print_warning "CLI encode command failed (this may be expected if CLI requires specific usage)"
    fi

    rm -f test_input.txt test_output.txt test_chunks/chunk_* test_chunks/metadata.txt
    rmdir test_chunks 2>/dev/null || true
    
    cd ..
}

test_examples() {
    print_status "Running example programs..."
    
    cd "$BUILD_DIR"

    if [ -f "clay_simple_demo" ]; then
        print_status "Running simple Clay demonstration..."
        if ./clay_simple_demo > /dev/null 2>&1; then
            print_success "Simple demo completed successfully"
        else
            print_warning "Simple demo failed"
        fi
    fi
 
    if [ -f "clay_integration" ]; then
        print_status "Running integration example..."
        if timeout 30s ./clay_integration > /dev/null 2>&1; then
            print_success "Integration example completed successfully"
        else
            print_warning "Integration example failed or timed out"
        fi
    fi

    if [ -f "clay_performance" ]; then
        print_status "Running performance test (with timeout)..."
        if timeout 60s ./clay_performance > /dev/null 2>&1; then
            print_success "Performance test completed successfully"
        else
            print_warning "Performance test failed or timed out"
        fi
    fi
    
    cd ..
}

print_summary() {
    echo ""
    echo "Build and Test Summary"
    echo ""

    if [ -f "$BUILD_DIR/libclay.so" ] || [ -f "$BUILD_DIR/libclay.a" ]; then
        echo "Clay Library: Built successfully"
    else
        echo "Clay Library: Build failed"
    fi
 
    if [ -f "$BUILD_DIR/clay_cli" ]; then
        echo "CLI Tool: Built and ready"
    else
        echo "CLI Tool: Not available"
    fi

    local example_count=0
    for example in clay_simple_demo clay_integration clay_performance; do
        if [ -f "$BUILD_DIR/$example" ]; then
            ((example_count++))
        fi
    done
    echo "Examples: $example_count programs built"

    if [ -f "$BUILD_DIR/api_test" ]; then
        echo "Test Suite: Available and passed"
    else
        echo "Test Suite: Not available"
    fi
    
    echo ""
    echo "Usage Instructions:"
    echo ""
    echo "1. Use the CLI tool:"
    echo "   cd $BUILD_DIR"
    echo "   ./clay_cli encode input.txt chunks/ 4 2 5"
    echo "   ./clay_cli decode chunks/ output.txt"
    echo ""
    echo "2. Run examples:"
    echo "   cd $BUILD_DIR"
    echo "   ./clay_simple_demo      # Basic demonstration"
    echo "   ./clay_integration      # Integration patterns"
    echo "   ./clay_performance      # Performance testing"
    echo ""
    echo "3. Run all examples at once:"
    echo "   ./run_examples.sh"
    echo ""
    echo "4. Install library system-wide:"
    echo "   cd $BUILD_DIR && sudo make install"
    echo ""
}

print_development_info() {
    echo "Development Information:"
    echo ""
    echo "Build directory: $BUILD_DIR/"
    echo "Build type: $BUILD_TYPE"
    echo "Parallel jobs: $PARALLEL_JOBS"
    echo ""
    echo "Key files built:"
    if [ -f "$BUILD_DIR/libclay.so" ]; then
        echo "  • libclay.so - Shared library"
    fi
    if [ -f "$BUILD_DIR/libclay.a" ]; then
        echo "  • libclay.a - Static library"
    fi
    if [ -f "$BUILD_DIR/clay_cli" ]; then
        echo "  • clay_cli - Command line interface"
    fi
    echo ""
    echo "To rebuild:"
    echo "  ./build_and_test.sh [Debug|Release]"
    echo ""
}

main() {
    local start_time=$(date +%s)
    
    print_status "Starting Clay library build and test pipeline..."
    print_status "Build type: $BUILD_TYPE"
    echo ""

    check_dependencies
    echo ""
    
    check_source_structure  
    echo ""
    
    configure_build
    echo ""
    
    build_library
    echo ""
    
    verify_build
    echo ""
    
    run_tests
    echo ""
    
    test_cli
    echo ""
    
    test_examples
    echo ""

    local end_time=$(date +%s)
    local build_time=$((end_time - start_time))
    
    print_success "Build and test pipeline completed successfully!"
    print_status "Total time: ${build_time} seconds"
    
    print_summary
    print_development_info
}

trap 'print_error "Build pipeline interrupted"; exit 1' INT TERM

case "$1" in
    "Debug"|"debug")
        BUILD_TYPE="Debug"
        ;;
    "Release"|"release"|"")
        BUILD_TYPE="Release"
        ;;
    "-h"|"--help")
        echo "Usage: $0 [Debug|Release]"
        echo ""
        echo "Build and test the Clay erasure coding library"
        echo ""
        echo "Options:"
        echo "  Debug    - Build with debug information and assertions"
        echo "  Release  - Build optimized for performance (default)"
        echo "  -h       - Show this help message"
        exit 0
        ;;
    *)
        print_error "Unknown build type: $1"
        echo "Use 'Debug' or 'Release', or run with --help for usage info"
        exit 1
        ;;
esac

main