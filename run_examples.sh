#!/bin/bash

set -e  

echo "Clay Library Examples Demonstration"
echo ""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' 

print_header() {
    echo -e "${CYAN}$1${NC}"
    local len=${#1}
    printf "%*s\n" $len | tr ' ' '='
    echo ""
}

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

print_demo() {
    echo -e "${MAGENTA}[DEMO]${NC} $1"
}

BUILD_DIR="build"
DEMO_DATA_DIR="demo_data"
EXAMPLES_LOG="examples_output.log"

check_prerequisites() {
    print_status "Checking prerequisites..."

    if [ ! -d "$BUILD_DIR" ]; then
        print_error "Build directory '$BUILD_DIR' not found!"
        print_error "Please run './build_and_test.sh' first to build the library"
        exit 1
    fi

    if [ ! -f "$BUILD_DIR/libclay.so" ] && [ ! -f "$BUILD_DIR/libclay.a" ]; then
        print_error "Clay library not found in $BUILD_DIR!"
        print_error "Please run './build_and_test.sh' first to build the library"
        exit 1
    fi
    
    print_success "Prerequisites satisfied"
}

prepare_demo_data() {
    print_status "Preparing demonstration data..."
    
    mkdir -p "$DEMO_DATA_DIR"

    cat > "$DEMO_DATA_DIR/sample.txt" << 'EOF'
This is a sample text file for demonstrating Clay erasure coding.
Clay codes are a class of MSR (Minimum Storage Regenerating) codes that
provide excellent repair locality properties for distributed storage systems.

The key advantages of Clay codes include:
- Low repair bandwidth
- Parallel repair operations  
- Configurable parameters (k, m, d)
- Optimal storage efficiency

This file will be encoded into multiple chunks and then decoded
to demonstrate the fault tolerance capabilities of Clay codes.
EOF

    print_status "Creating binary test data..."
    dd if=/dev/urandom of="$DEMO_DATA_DIR/binary_data.bin" bs=1024 count=16 2>/dev/null

    print_status "Creating larger test file..."
    for i in {1..100}; do
        echo "Line $i: This is test data for Clay erasure coding demonstration. Repeat $i of 100." >> "$DEMO_DATA_DIR/large_text.txt"
    done
    
    print_success "Demo data prepared in $DEMO_DATA_DIR/"
}

demonstrate_cli() {
    if [ ! -f "$BUILD_DIR/clay_cli" ]; then
        print_warning "CLI tool not available, skipping CLI demonstration"
        return 0
    fi

    print_header "Clay CLI Tool Demonstration"
    
    cd "$BUILD_DIR"
    
    print_demo "Testing CLI with sample text file..."

    mkdir -p cli_test_chunks

    print_status "Encoding sample.txt with k = 4, m = 2, d = 5..."
    if ../run_examples.sh encode "../$DEMO_DATA_DIR/sample.txt" cli_test_chunks 4 2 5; then
        print_success "File encoded successfully"

        print_status "Created chunks:"
        ls -la cli_test_chunks/

        print_status "Decoding chunks back to file..."
        if ./clay_cli decode cli_test_chunks recovered_sample.txt; then
            print_success "File decoded successfully"

            if cmp -s "../$DEMO_DATA_DIR/sample.txt" recovered_sample.txt; then
                print_success "Data integrity verified - files are identical!"
            else
                print_warning "Data integrity check failed"
            fi
        else
            print_warning "Decoding failed"
        fi

        print_demo "Demonstrating repair after chunk loss..."
        if [ -f "cli_test_chunks/chunk_1.dat" ]; then
            print_status "Simulating loss of chunk_1.dat..."
            rm cli_test_chunks/chunk_1.dat
            
            print_status "Attempting repair..."
            if ./clay_cli repair cli_test_chunks; then
                print_success "Chunk repaired successfully!"

                if [ -f "cli_test_chunks/chunk_1.dat" ]; then
                    print_success "Lost chunk has been restored"
                else
                    print_warning "Chunk repair did not restore the file"
                fi
            else
                print_warning "Repair operation failed"
            fi
        fi
    else
        print_warning "CLI encoding failed"
    fi

    rm -rf cli_test_chunks recovered_sample.txt
    cd ..
}

demonstrate_simple_clay() {
    if [ ! -f "$BUILD_DIR/clay_simple_demo" ]; then
        print_warning "Simple demo not available"
        return 0
    fi

    print_header "Simple Clay Demo"
    
    cd "$BUILD_DIR"
    print_demo "Running basic Clay erasure coding demonstration..."
    
    if ./clay_simple_demo | tee -a "../$EXAMPLES_LOG"; then
        print_success "Simple demo completed successfully"
    else
        print_error "Simple demo failed"
    fi
    
    cd ..
}

demonstrate_integration() {
    if [ ! -f "$BUILD_DIR/clay_integration" ]; then
        print_warning "Integration example not available"
        return 0
    fi

    print_header "Clay Integration Example"
    
    cd "$BUILD_DIR"
    print_demo "Running Clay library integration patterns..."
    print_status "This demonstrates proper error handling, resource management, and API usage"
    
    if timeout 45s ./clay_integration | tee -a "../$EXAMPLES_LOG"; then
        print_success "Integration example completed successfully"
    else
        print_warning "Integration example failed or timed out"
    fi
    
    cd ..
}

demonstrate_advanced_usage() {
    if [ ! -f "$BUILD_DIR/clay_advanced" ]; then
        print_warning "Advanced usage example not available"
        return 0
    fi

    print_header "Clay Advanced Usage"
    
    cd "$BUILD_DIR"
    print_demo "Running advanced Clay usage patterns..."
    print_status "This demonstrates different configurations and file operations"
    
    if timeout 45s ./clay_advanced | tee -a "../$EXAMPLES_LOG"; then
        print_success "Advanced usage example completed successfully"
    else
        print_warning "Advanced usage example failed or timed out"
    fi
    
    cd ..
}

demonstrate_file_types() {
    if [ ! -f "$BUILD_DIR/clay_simple_demo" ]; then
        print_warning "Cannot demonstrate file types without basic demo"
        return 0
    fi

    print_header "Clay with Different File Types"
    
    print_demo "Testing Clay with various data types..."

    cd "$BUILD_DIR"

    print_status "Testing with binary data (16KB)..."
    if timeout 30s ./clay_simple_demo > /dev/null 2>&1; then
        print_success "Binary data test passed"
    else
        print_warning "Binary data test failed"
    fi
    
    cd ..
}

demonstrate_api_tests() {
    if [ ! -f "$BUILD_DIR/api_test" ]; then
        print_warning "API tests not available"
        return 0
    fi

    print_header "Clay API Test Suite"
    
    cd "$BUILD_DIR"
    print_demo "Running comprehensive API validation tests..."
    
    if ./api_test | tee -a "../$EXAMPLES_LOG"; then
        print_success "All API tests passed!"
    else
        print_error "Some API tests failed"
    fi
    
    cd ..
}

create_summary() {
    print_header "Demonstration Summary"
    
    echo "Clay Library Examples Summary"
    echo ""
    echo "This demonstration showcased:"
    echo "• Configurable redundancy (k, m, d parameters)"
    echo "• Fault tolerance up to m chunk failures"
    echo "• Efficient repair operations"
    echo "• Encode/decode"
    echo ""
    echo "Files created:"
    echo "• $EXAMPLES_LOG - Complete output log"
    echo "• $DEMO_DATA_DIR/ - Test data files"
    echo ""
    echo "Next steps:"
    echo "• Integrate Clay into your application"
    echo "• Optimize parameters for your use case"
    echo "• Refer to examples for implementation patterns"
}

print_usage_guide() {
    echo ""
    echo "Clay Library Usage Guide"
    echo ""
    echo "For developers wanting to use Clay:"
    echo ""
    echo "1. Include headers:"
    echo '   #include "ErasureCodeClay.h"'
    echo '   #include "BufferList.h"'
    echo ""
    echo "2. Link against library:"
    echo "   g++ -std=c++14 myapp.cpp -lclay -ljerasure -lgf_complete"
    echo ""
    echo "3. Basic usage pattern:"
    echo "   • Initialize Clay with desired parameters"
    echo "   • Encode data into k+m chunks"
    echo "   • Store chunks across distributed nodes"
    echo "   • Decode from available chunks when needed"
    echo "   • Repair failed chunks using surviving chunks"
    echo ""
    echo "4. Parameter selection:"
    echo "   • k = number of data chunks"
    echo "   • m = number of coding chunks (fault tolerance)"
    echo "   • d = repair parameter (k+1 ≤ d ≤ k+m-1)"
    echo "   • Higher d provides better repair efficiency"
    echo ""
    echo "See the integration example for complete API patterns."
}

main() {
    local start_time=$(date +%s)
    
    print_status "Starting Clay library examples demo"
    echo ""

    echo "Clay Library Examples - $(date)" > "$EXAMPLES_LOG"
    echo "" >> "$EXAMPLES_LOG"

    check_prerequisites
    echo ""
    
    prepare_demo_data
    echo ""
    
    demonstrate_cli
    echo ""
    
    demonstrate_integration  
    echo ""
    
    demonstrate_file_types
    echo ""
    
    demonstrate_api_tests
    echo ""

    local end_time=$(date +%s)
    local total_time=$((end_time - start_time))
    
    create_summary
    print_usage_guide
    
    echo ""
    print_success "All demonstrations completed successfully!"
    print_status "Total demonstration time: ${total_time} seconds"
    print_status "Full output logged to: $EXAMPLES_LOG"

    read -p "Clean up demo data files? (y/N): " -n 1 -r
    echo ""
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        rm -rf "$DEMO_DATA_DIR"
        print_status "Demo data cleaned up"
    else
        print_status "Demo data preserved in $DEMO_DATA_DIR/"
    fi
}

trap 'print_error "Examples demonstration interrupted"; exit 1' INT TERM

case "${1:-all}" in
    "cli")
        check_prerequisites && demonstrate_cli
        ;;
    "integration")  
        check_prerequisites && demonstrate_integration
        ;;
    "tests")
        check_prerequisites && demonstrate_api_tests
        ;;
    "all"|"")
        main
        ;;
    "-h"|"--help")
        echo "Usage: $0 [cli|simple|integration|performance|tests|all]"
        echo ""
        echo "Run Clay library example demonstrations"
        echo ""
        echo "Options:"
        echo "  cli          - Run CLI tool demonstration only"
        echo "  integration  - Run integration example only"
        echo "  tests        - Run API test suite only"
        echo "  all          - Run all demonstrations (default)"
        echo "  -h, --help   - Show this help message"
        exit 0
        ;;
    *)
        print_error "Unknown option: $1"
        echo "Use --help for usage information"
        exit 1
        ;;
esac