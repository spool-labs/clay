#include "ErasureCodeClay.h"
#include "BufferList.h"
#include <iostream>
#include <sstream>
#include <memory>
#include <vector>
#include <fstream>

void demonstrate_different_configurations() {
    std::cout << "Different Clay Configurations" << std::endl;
    
    struct Config {
        int k, m, d;
        std::string description;
    };
    
    std::vector<Config> configs = {
        {4, 2, 5, "Standard configuration (4+2)"},
        {6, 3, 8, "Higher redundancy (6+3)"},
        {8, 4, 10, "Large distributed system (8+4)"},
        {3, 2, 4, "Small cluster (3+2)"}
    };
    
    const std::string test_data = "This is test data for demonstrating different Clay configurations.";
    
    for (const auto& config : configs) {
        std::cout << "\nTesting: " << config.description << std::endl;
        std::cout << "Parameters: k=" << config.k << ", m=" << config.m << ", d=" << config.d << std::endl;
        
        ErasureCodeClay clay(".");
        ErasureCodeProfile profile;
        profile["k"] = std::to_string(config.k);
        profile["m"] = std::to_string(config.m);
        profile["d"] = std::to_string(config.d);
        profile["scalar_mds"] = "jerasure";
        profile["technique"] = "reed_sol_van";

        std::ostringstream oss;
        int result = clay.init(profile, &oss);
        if (result != 0) {
            std::cout << "  Failed to initialize: " << oss.str() << std::endl;
            continue;
        }

        double overhead = (static_cast<double>(config.m) / config.k) * 100;
        std::cout << "  Total chunks: " << clay.get_chunk_count() << std::endl;
        std::cout << "  Storage overhead: " << overhead << "%" << std::endl;
        std::cout << "  Fault tolerance: up to " << config.m << " chunk failures" << std::endl;

        bufferptr input_ptr = buffer::create_aligned(test_data.size(), ErasureCode::SIMD_ALIGN);
        input_ptr.copy_in(0, test_data.size(), test_data.c_str());
        bufferlist input;
        input.push_back(std::move(input_ptr));
        
        std::set<int> want_to_encode;
        for (int i = 0; i < clay.get_chunk_count(); ++i) {
            want_to_encode.insert(i);
        }
        
        std::map<int, bufferlist> encoded;
        result = clay.encode(want_to_encode, input, &encoded);
        if (result == 0) {
            size_t chunk_size = encoded.begin()->second.length();
            std::cout << "  Chunk size: " << chunk_size << " bytes" << std::endl;
            std::cout << "  Configuration works correctly!" << std::endl;
        } else {
            std::cout << "  Encoding failed" << std::endl;
        }
    }
}

void demonstrate_file_handling() {
    std::cout << "\nFile-Based Operations" << std::endl;

    const std::string filename = "test_file.txt";
    std::ofstream file(filename);
    file << "This is a test file for demonstrating Clay file operations.\n";
    file << "It contains multiple lines of text.\n";
    file << "Clay can encode this file into chunks for distributed storage.\n";
    file.close();

    std::ifstream infile(filename, std::ios::binary | std::ios::ate);
    size_t file_size = infile.tellg();
    infile.seekg(0);
    
    std::vector<char> file_data(file_size);
    infile.read(file_data.data(), file_size);
    infile.close();
    
    std::cout << "File size: " << file_size << " bytes" << std::endl;

    ErasureCodeClay clay(".");
    ErasureCodeProfile profile;
    profile["k"] = "4";
    profile["m"] = "2";
    profile["d"] = "5";
    profile["scalar_mds"] = "jerasure";
    profile["technique"] = "reed_sol_van";

    std::ostringstream oss;
    clay.init(profile, &oss);

    bufferptr input_ptr = buffer::create_aligned(file_size, ErasureCode::SIMD_ALIGN);
    input_ptr.copy_in(0, file_size, file_data.data());
    bufferlist input;
    input.push_back(std::move(input_ptr));
    
    std::set<int> want_to_encode;
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        want_to_encode.insert(i);
    }
    
    std::map<int, bufferlist> encoded;
    int result = clay.encode(want_to_encode, input, &encoded);
    if (result != 0) {
        std::cout << "File encoding failed" << std::endl;
        return;
    }
    
    std::cout << "File encoded into " << encoded.size() << " chunks" << std::endl;
    
    for (const auto& p : encoded) {
        int index = p.first;
        const bufferlist& chunk = p.second;
        std::string chunk_filename = "chunk_" + std::to_string(index) + ".dat";
        std::ofstream chunk_file(chunk_filename, std::ios::binary);
        // shallow copy to non-const temp, then c_str() + length()
        bufferlist temp_chunk = chunk;  
        const char* data = temp_chunk.c_str();
        size_t len = temp_chunk.length();
        chunk_file.write(data, len);
        chunk_file.close();
        std::cout << "Saved " << chunk_filename << " (" << chunk.length() << " bytes)" << std::endl;
    }

    std::map<int, bufferlist> available_chunks;
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        if (i == 1) continue; 
        
        std::string chunk_filename = "chunk_" + std::to_string(i) + ".dat";
        std::ifstream chunk_file(chunk_filename, std::ios::binary | std::ios::ate);
        size_t chunk_size = chunk_file.tellg();
        chunk_file.seekg(0);
        
        bufferptr chunk_ptr = buffer::create_aligned(chunk_size, ErasureCode::SIMD_ALIGN);
        chunk_file.read(chunk_ptr.c_str(), chunk_size);
        chunk_file.close();
        
        available_chunks[i].push_back(std::move(chunk_ptr));
    }
    
    std::cout << "Simulated loss of chunk_1.dat, attempting recovery..." << std::endl;

    std::set<int> want_to_read;
    for (int i = 0; i < clay.k; ++i) {
        want_to_read.insert(i);
    }
    
    std::map<int, bufferlist> decoded;
    size_t chunk_size = encoded.begin()->second.length();
    result = clay.decode(want_to_read, available_chunks, &decoded, chunk_size);
    if (result != 0) {
        std::cout << "File decoding failed" << std::endl;
        return;
    }

    bufferlist reconstructed;
    for (int i = 0; i < clay.k; ++i) {
        reconstructed.append(decoded[i]);
    }

    std::string recovered_filename = "recovered_" + filename;
    std::ofstream recovered_file(recovered_filename, std::ios::binary);
    std::vector<char> recovered_data(reconstructed.length());
    buffer::list_iterator it(reconstructed.begin(0));
    it.copy(reconstructed.length(), recovered_data.data());
    recovered_file.write(recovered_data.data(), recovered_data.size());
    recovered_file.close();

    std::ifstream original_check(filename, std::ios::binary);
    std::ifstream recovered_check(recovered_filename, std::ios::binary);
    
    bool files_match = true;
    char orig_char, rec_char;
    while (original_check.get(orig_char) && recovered_check.get(rec_char)) {
        if (orig_char != rec_char) {
            files_match = false;
            break;
        }
    }

    if (files_match && original_check.eof() && recovered_check.eof()) {
        std::cout << "File recovery successful - data integrity verified!" << std::endl;
    } else {
        std::cout << "File recovery failed - data corruption detected" << std::endl;
    }

    std::remove(filename.c_str());
    std::remove(recovered_filename.c_str());
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        std::string chunk_filename = "chunk_" + std::to_string(i) + ".dat";
        std::remove(chunk_filename.c_str());
    }
}

void demonstrate_error_scenarios() {
    std::cout << "\nError Handling" << std::endl;
    
    ErasureCodeClay clay(".");
    ErasureCodeProfile profile;
    profile["k"] = "4";
    profile["m"] = "2";
    profile["d"] = "5";
    profile["scalar_mds"] = "jerasure";
    profile["technique"] = "reed_sol_van";

    std::ostringstream oss;
    clay.init(profile, &oss);
    
    std::cout << "Testing insufficient chunks scenario..." << std::endl;
    
    std::map<int, bufferlist> insufficient_chunks;
    bufferptr dummy_ptr = buffer::create_aligned(100, ErasureCode::SIMD_ALIGN);
    bufferlist dummy_chunk;
    dummy_chunk.push_back(std::move(dummy_ptr));

    insufficient_chunks[0] = dummy_chunk;
    insufficient_chunks[1] = dummy_chunk;  
    insufficient_chunks[2] = dummy_chunk;
    
    std::set<int> want_to_read;
    for (int i = 0; i < clay.k; ++i) {
        want_to_read.insert(i);
    }
    
    std::map<int, bufferlist> decoded;
    int result = clay.decode(want_to_read, insufficient_chunks, &decoded, 100);
    if (result != 0) {
        std::cout << "  Correctly rejected insufficient chunks" << std::endl;
    } else {
        std::cout << "  ERROR: Should have failed with insufficient chunks" << std::endl;
    }

    std::cout << "Testing beyond fault tolerance..." << std::endl;

    const std::string test_data = "Test data for error scenarios";
    bufferptr input_ptr = buffer::create_aligned(test_data.size(), ErasureCode::SIMD_ALIGN);
    input_ptr.copy_in(0, test_data.size(), test_data.c_str());
    bufferlist input;
    input.push_back(std::move(input_ptr));
    
    std::set<int> want_to_encode;
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        want_to_encode.insert(i);
    }
    
    std::map<int, bufferlist> encoded;
    clay.encode(want_to_encode, input, &encoded);

    auto insufficient_for_decode = encoded;
    insufficient_for_decode.erase(1);
    insufficient_for_decode.erase(2);
    insufficient_for_decode.erase(3); // remove 3 chunks, it exceeds m = 2 limit
    
    result = clay.decode(want_to_read, insufficient_for_decode, &decoded, encoded.begin()->second.length());
    if (result != 0) {
        std::cout << "  Correctly failed when beyond fault tolerance" << std::endl;
    } else {
        std::cout << "  ERROR: Should have failed beyond fault tolerance" << std::endl;
    }
}

int main() {
    std::cout << "Clay Advanced Usage Examples" << std::endl;
    
    try {
        demonstrate_different_configurations();
        demonstrate_file_handling();
        demonstrate_error_scenarios();

        std::cout << "Advanced usage demo completed!" << std::endl;
        std::cout << "This example showed:" << std::endl;
        std::cout << "• Different Clay parameter configurations" << std::endl;
        std::cout << "• File-based encoding and decoding operations" << std::endl;
        std::cout << "• Proper error handling and edge cases" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}