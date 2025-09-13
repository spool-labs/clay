#include <clay/ErasureCodeClay.h>
#include <clay/ErasureCodeInterface.h>
#include <clay/BufferList.h>
#include <iostream>
#include <vector>
#include <iomanip>

void print_buffer_hex(const std::string& label, const char* data, size_t size, int max_bytes = 32) {
    std::cout << label << " (" << size << " bytes):" << std::endl;
    
    for (int i = 0; i < std::min(static_cast<int>(size), max_bytes); i++) {
        if (i % 16 == 0) std::cout << "  ";
        std::printf("%02x ", static_cast<unsigned char>(data[i]));
        if (i % 16 == 15) std::cout << std::endl;
    }
    if (size > max_bytes) {
        std::cout << "  ... (" << (size - max_bytes) << " more bytes)" << std::endl;
    }
    if (max_bytes % 16 != 0) std::cout << std::endl;
}

void debug_bufferlist(const std::string& label, const BufferList& bl) {
    std::cout << "\n" << label << ":" << std::endl;
    std::cout << "  Length: " << bl.length() << " bytes" << std::endl;
    
    if (bl.length() > 0) {
        const char* data = bl.c_str();
        print_buffer_hex("  Content", data, bl.length(), 64);
        
        // Check if all zeros
        bool all_zeros = true;
        for (size_t i = 0; i < bl.length(); i++) {
            if (data[i] != 0) {
                all_zeros = false;
                break;
            }
        }
        std::cout << "  All zeros: " << (all_zeros ? "YES" : "NO") << std::endl;
    }
}

int main() {
    std::cout << "Clay Encoding Debug Session" << std::endl;
    std::cout << "===========================" << std::endl;
    
    // Step 1: Create test data with clear pattern
    const size_t data_size = 64; // Smaller size for easier debugging
    std::vector<uint8_t> test_data(data_size);
    
    for (size_t i = 0; i < data_size; i++) {
        test_data[i] = static_cast<uint8_t>(i + 1); // 1, 2, 3, 4... (avoid zeros)
    }
    
    std::cout << "\nStep 1: Input Data Creation" << std::endl;
    print_buffer_hex("Original data", reinterpret_cast<const char*>(test_data.data()), data_size);
    
    // Step 2: Initialize Clay
    std::cout << "\nStep 2: Clay Initialization" << std::endl;
    ErasureCodeClay clay;
    ErasureCodeProfile profile;
    profile["k"] = "2";  // Smaller config for debugging
    profile["m"] = "1";  
    profile["d"] = "2";
    profile["jerasure-per-chunk-alignment"] = "false";
    
    std::cout << "Profile: k=2, m=1, d=2" << std::endl;
    
    int init_result = clay.init(profile, nullptr);
    std::cout << "Init result: " << init_result << std::endl;
    
    if (init_result != 0) {
        std::cout << "FAILED: Clay initialization failed" << std::endl;
        return 1;
    }
    
    // Step 3: Create BufferList from input
    std::cout << "\nStep 3: BufferList Creation" << std::endl;
    BufferList input_bl;
    input_bl.append(reinterpret_cast<const char*>(test_data.data()), data_size);
    
    debug_bufferlist("Input BufferList", input_bl);
    
    // Step 4: Check Clay parameters
    std::cout << "\nStep 4: Clay Parameters Check" << std::endl;
    std::cout << "Chunk count: " << clay.get_chunk_count() << std::endl;
    std::cout << "Data chunk count: " << clay.get_data_chunk_count() << std::endl;
    std::cout << "Chunk size for " << data_size << " bytes: " << clay.get_chunk_size(data_size) << std::endl;
    
    // Step 5: Prepare encoding
    std::cout << "\nStep 5: Encoding Preparation" << std::endl;
    std::set<int> want_to_encode;
    int total_chunks = clay.get_chunk_count();
    for (int i = 0; i < total_chunks; i++) {
        want_to_encode.insert(i);
    }
    
    std::cout << "Chunks to encode: ";
    for (int chunk : want_to_encode) {
        std::cout << chunk << " ";
    }
    std::cout << std::endl;
    
    // Step 6: Perform encoding
    std::cout << "\nStep 6: Encoding Operation" << std::endl;
    std::map<int, BufferList> encoded_chunks;
    
    int encode_result = clay.encode(want_to_encode, input_bl, &encoded_chunks);
    std::cout << "Encode result: " << encode_result << std::endl;
    
    if (encode_result != 0) {
        std::cout << "FAILED: Encoding failed with code " << encode_result << std::endl;
        return 1;
    }
    
    // Step 7: Analyze results
    std::cout << "\nStep 7: Results Analysis" << std::endl;
    std::cout << "Number of chunks generated: " << encoded_chunks.size() << std::endl;
    
    bool found_non_zero = false;
    for (const auto& pair : encoded_chunks) {
        int chunk_id = pair.first;
        const BufferList& chunk_bl = pair.second;
        
        std::cout << "\nChunk " << chunk_id << ":" << std::endl;
        debug_bufferlist("", chunk_bl);
        
        // Check for non-zero content
        if (chunk_bl.length() > 0) {
            const char* data = chunk_bl.c_str();
            for (size_t i = 0; i < chunk_bl.length(); i++) {
                if (data[i] != 0) {
                    found_non_zero = true;
                    break;
                }
            }
        }
    }
    
    // Step 8: Diagnosis
    std::cout << "\nStep 8: Diagnosis" << std::endl;
    std::cout << "Found non-zero data: " << (found_non_zero ? "YES" : "NO") << std::endl;
    
    if (!found_non_zero) {
        std::cout << "\nPROBLEM IDENTIFIED: All chunks contain zeros" << std::endl;
        std::cout << "This indicates one of the following issues:" << std::endl;
        std::cout << "1. Clay algorithm not properly initialized" << std::endl;
        std::cout << "2. Jerasure library not functioning correctly" << std::endl;
        std::cout << "3. Buffer management issue (data not copied properly)" << std::endl;
        std::cout << "4. Clay parameters invalid for this data size" << std::endl;
        
        // Additional diagnostics
        std::cout << "\nAdditional checks:" << std::endl;
        
        // Check if input data survived the BufferList conversion
        std::cout << "Input BufferList contains non-zero data: ";
        bool input_has_data = false;
        if (input_bl.length() > 0) {
            const char* input_data = input_bl.c_str();
            for (size_t i = 0; i < input_bl.length(); i++) {
                if (input_data[i] != 0) {
                    input_has_data = true;
                    break;
                }
            }
        }
        std::cout << (input_has_data ? "YES" : "NO") << std::endl;
        
        if (!input_has_data) {
            std::cout << "CRITICAL: Input data lost during BufferList conversion" << std::endl;
        }
        
    } else {
        std::cout << "\nSUCCESS: Encoding produced valid output" << std::endl;
    }
    
    return 0;
}
