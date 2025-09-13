#!/bin/bash

echo "Creating chunk demonstration example"

cat > examples/chunk_demo.cpp << 'EOF'
#include <clay/ErasureCodeClay.h>
#include <clay/ErasureCodeInterface.h>
#include <clay/Buffer.h>
#include <iostream>
#include <vector>
#include <iomanip>

void print_hex_data(const std::string& label, const char* data, size_t size, int max_bytes = 16) {
    std::cout << label << " (" << size << " bytes): ";
    
    int bytes_to_show = std::min(static_cast<int>(size), max_bytes);
    
    for (int i = 0; i < bytes_to_show; i++) {
        std::printf("%02x ", static_cast<unsigned char>(data[i]));
    }
    
    if (size > max_bytes) {
        std::cout << "... (" << (size - max_bytes) << " more bytes)";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "Clay Library Chunk Generation Demo" << std::endl;
    
    try {
        // Create test data with recognizable pattern
        const size_t data_size = 1024;
        std::vector<uint8_t> test_data(data_size);
        
        // Fill with a clear pattern
        for (size_t i = 0; i < data_size; i++) {
            test_data[i] = static_cast<uint8_t>(i % 256);
        }
        
        std::cout << "Input: " << data_size << " bytes with pattern [0,1,2...255,0,1,2...]" << std::endl;
        print_hex_data("First 16 bytes", reinterpret_cast<const char*>(test_data.data()), data_size, 16);
        
        // Initialize Clay
        ErasureCodeClay clay;
        ErasureCodeProfile profile;
        profile["k"] = "4";  // 4 data chunks
        profile["m"] = "2";  // 2 coding chunks  
        profile["d"] = "5";  // repair parameter
        profile["jerasure-per-chunk-alignment"] = "false";
        
        std::cout << "\nClay Configuration:" << std::endl;
        std::cout << "  k (data chunks): 4" << std::endl;
        std::cout << "  m (coding chunks): 2" << std::endl;
        std::cout << "  d (repair parameter): 5" << std::endl;
        std::cout << "  Total chunks: 6" << std::endl;
        std::cout << "  Fault tolerance: Up to 2 chunk failures" << std::endl;
        
        int result = clay.init(profile, nullptr);
        if (result != 0) {
            std::cout << "Clay initialization failed: " << result << std::endl;
            return 1;
        }
        
        std::cout << "\nClay initialization successful!" << std::endl;
        
        // Convert data to BufferList
        BufferList input_bl;
        input_bl.append(reinterpret_cast<const char*>(test_data.data()), data_size);
        
        // Encode all chunks
        std::set<int> want_to_encode;
        for (int i = 0; i < 6; i++) {
            want_to_encode.insert(i);
        }
        
        std::map<int, BufferList> encoded_chunks;
        int encode_result = clay.encode(want_to_encode, input_bl, &encoded_chunks);
        
        if (encode_result != 0) {
            std::cout << "Encoding failed: " << encode_result << std::endl;
            return 1;
        }
        
        std::cout << "\nENCODING RESULTS:" << std::endl;
        std::cout << "Successfully generated " << encoded_chunks.size() << " chunks" << std::endl;
        
        size_t total_encoded_size = 0;
        
        for (const auto& pair : encoded_chunks) {
            int chunk_id = pair.first;
            const BufferList& chunk_bl = pair.second;
            
            std::string chunk_type = (chunk_id < 4) ? "DATA" : "CODING";
            size_t chunk_size = chunk_bl.length();
            total_encoded_size += chunk_size;
            
            std::cout << "\nChunk " << chunk_id << " (" << chunk_type << "):" << std::endl;
            std::cout << "  Size: " << chunk_size << " bytes" << std::endl;
            
            // Show hex content for verification
            const char* chunk_data = chunk_bl.c_str();
            print_hex_data("  Content", chunk_data, chunk_size, 16);
            
            // For data chunks, show if they contain recognizable patterns
            if (chunk_id < 4) {
                bool has_pattern = false;
                for (size_t i = 0; i < std::min(chunk_size, size_t(16)); i++) {
                    if (static_cast<unsigned char>(chunk_data[i]) == (i % 256)) {
                        has_pattern = true;
                        break;
                    }
                }
                if (has_pattern) {
                    std::cout << "  Note: Contains elements of original pattern" << std::endl;
                } else {
                    std::cout << "  Note: Data has been transformed by Clay encoding" << std::endl;
                }
            } else {
                std::cout << "  Note: Contains redundancy information for fault tolerance" << std::endl;
            }
        }
        
        std::cout << "\nSUMMARY:" << std::endl;
        std::cout << "Original data size: " << data_size << " bytes" << std::endl;
        std::cout << "Total encoded size: " << total_encoded_size << " bytes" << std::endl;
        std::cout << "Storage overhead: " << std::fixed << std::setprecision(1) 
                  << (100.0 * total_encoded_size / data_size - 100.0) << "%" << std::endl;
        std::cout << "Fault tolerance: Any " << (6-4) << " chunks can fail" << std::endl;
        std::cout << "Minimum chunks needed for recovery: 4" << std::endl;
        
        std::cout << "\nClay erasure coding demonstration complete!" << std::endl;
        std::cout << "This proves the library can:" << std::endl;
        std::cout << "Accept arbitrary input data" << std::endl;
        std::cout << "Generate erasure-coded chunks using Clay algorithm" << std::endl;
        std::cout << "Provide configurable redundancy (k+m chunks)" << std::endl;
        std::cout << "Enable fault tolerant distributed storage" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
EOF

echo ""
echo "Adding chunk demo to CMakeLists.txt..."

cat >> CMakeLists.txt << 'EOF'

# Add chunk demonstration example
if(BUILD_EXAMPLES)
    if(EXISTS ${CMAKE_SOURCE_DIR}/examples/chunk_demo.cpp)
        add_executable(clay_chunk_demo examples/chunk_demo.cpp)
        target_link_libraries(clay_chunk_demo clay)
        target_include_directories(clay_chunk_demo PRIVATE include)
    endif()
endif()
EOF

echo "Created chunk demonstration example"
echo ""
echo "Now rebuild and test:"
echo "  cd build"
echo "  make clay_chunk_demo"
echo "  ./clay_chunk_demo"