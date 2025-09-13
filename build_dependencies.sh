#!/bin/bash

set -e 

echo "Building Clay Library Dependencies"
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

check_dependencies() {
    print_status "Checking build dependencies..."
    
    local missing_tools=()
    
    if ! command -v gcc &> /dev/null; then
        missing_tools+=("gcc")
    fi
    
    if ! command -v make &> /dev/null; then
        missing_tools+=("make")
    fi
    
    if ! command -v autoreconf &> /dev/null; then
        missing_tools+=("autotools-dev autoconf automake libtool")
    fi
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        print_error "Missing required tools: ${missing_tools[*]}"
        echo ""
        echo "Please install them using your package manager:"
        echo "  Ubuntu/Debian: sudo apt-get install build-essential autotools-dev autoconf automake libtool"
        echo "  CentOS/RHEL:   sudo yum groupinstall 'Development Tools' && sudo yum install autoconf automake libtool"
        echo "  macOS:         brew install automake autoconf libtool"
        exit 1
    fi
    
    print_success "All build tools available"
}

build_gf_complete() {
    print_status "Building GF-Complete library.."
    
    if [ ! -d "deps/gf-complete" ]; then
        print_error "GF-Complete source directory not found at deps/gf-complete"
        print_error "Please ensure the deps directory is properly set up"
        exit 1
    fi
    
    cd deps/gf-complete

    if [ -f "src/.libs/libgf_complete.a" ] || [ -f "src/.libs/libgf_complete.so" ]; then
        print_success "GF-Complete already built"
        cd ../..
        return 0
    fi

    if [ -f "Makefile" ]; then
        print_status "Cleaning previous GF-Complete build..."
        make clean > /dev/null 2>&1 || true
    fi

    if [ ! -f "configure" ]; then
        print_status "Generating GF-Complete configure script..."
        ./autogen.sh
    fi

    print_status "Configuring GF-Complete..."
    ./configure --quiet --enable-static --disable-shared

    print_status "Compiling GF-Complete..."
    make -j$(nproc) --quiet

    if [ -f "src/.libs/libgf_complete.a" ]; then
        print_success "GF-Complete built successfully"
    else
        print_error "GF-Complete build failed - library not found"
        exit 1
    fi
    
    cd ../..
}

build_jerasure() {
    print_status "Building Jerasure library..."
    
    if [ ! -d "deps/jerasure" ]; then
        print_error "Jerasure source directory not found at deps/jerasure"
        print_error "Please ensure the deps directory is properly set up"
        exit 1
    fi
    
    cd deps/jerasure

    if [ -f "src/.libs/libJerasure.a" ] || [ -f "src/.libs/libjerasure.a" ]; then
        print_success "Jerasure already built"
        cd ../..
        return 0
    fi

    if [ -f "Makefile" ]; then
        print_status "Cleaning previous Jerasure build..."
        make clean > /dev/null 2>&1 || true
    fi

    if [ ! -f "configure" ]; then
        print_status "Generating Jerasure configure script..."
        ./autogen.sh
    fi

    GF_COMPLETE_DIR="$(pwd)/../gf-complete"
    export LDFLAGS="-L${GF_COMPLETE_DIR}/src/.libs"
    export CPPFLAGS="-I${GF_COMPLETE_DIR}/include"

    print_status "Configuring Jerasure..."
    ./configure --quiet --enable-static --disable-shared

    print_status "Compiling Jerasure..."
    make -j$(nproc) --quiet

    if [ -f "src/.libs/libJerasure.a" ] || [ -f "src/.libs/libjerasure.a" ]; then
        print_success "Jerasure built successfully"
    else
        print_error "Jerasure build failed - library not found"
        print_error "Checked for: src/.libs/libJerasure.a and src/.libs/libjerasure.a"
        exit 1
    fi

    unset LDFLAGS CPPFLAGS
    
    cd ../..
}

verify_dependencies() {
    print_status "Verifying dependency builds..."
    
    local all_good=true

    if [ -f "deps/gf-complete/src/.libs/libgf_complete.a" ] || [ -f "deps/gf-complete/src/.libs/libgf_complete.so" ]; then
        print_success "GF-Complete library found"
    else
        print_error "GF-Complete library missing"
        all_good=false
    fi

    if [ -f "deps/gf-complete/include/gf_complete.h" ]; then
        print_success "GF-Complete headers found"
    else
        print_error "GF-Complete headers missing"
        all_good=false
    fi

    if [ -f "deps/jerasure/src/.libs/libJerasure.a" ] || [ -f "deps/jerasure/src/.libs/libjerasure.a" ] || 
       [ -f "deps/jerasure/src/.libs/libJerasure.so" ] || [ -f "deps/jerasure/src/.libs/libjerasure.so" ]; then
        print_success "Jerasure library found"
    else
        print_error "Jerasure library missing"
        all_good=false
    fi

    if [ -f "deps/jerasure/include/jerasure.h" ]; then
        print_success "Jerasure headers found"
    else
        print_error "Jerasure headers missing" 
        all_good=false
    fi
    
    if [ "$all_good" = true ]; then
        print_success "All dependencies built and verified successfully!"
        return 0
    else
        print_error "Some dependencies are missing or incomplete"
        return 1
    fi
}

print_next_steps() {
    echo ""
    echo "Dependencies Built Successfully!"
    echo ""
    echo "Next steps:"
    echo "  1. Run: ./build_and_test.sh"
    echo "     (This will build the Clay library and run tests)"
    echo ""  
    echo "  2. Or build manually:"
    echo "     mkdir -p build && cd build"
    echo "     cmake .. -DCMAKE_BUILD_TYPE=Release"
    echo "     make -j$(nproc)"
    echo ""
    echo "  3. Run examples:"
    echo "     ./run_examples.sh"
    echo ""
}

main() {
    check_dependencies
    echo ""
    
    build_gf_complete
    echo ""
    
    build_jerasure  
    echo ""
    
    if verify_dependencies; then
        print_next_steps
        exit 0
    else
        print_error "Dependency build verification failed"
        exit 1
    fi
}

trap 'print_error "Build interrupted"; exit 1' INT TERM

main "$@"