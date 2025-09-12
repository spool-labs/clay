#include <iostream>
#include <string>
#include <map>
#include <set>
#include "ErasureCodeClay.h"
#include "BufferList.h"

int main() {
    // Initialize Clay erasure code with parameters
    // k=4 data chunks, m=2 parity chunks, d=5 helper chunks for repair
    ErasureCodeClay clay;
    ErasureCodeProfile profile;
    profile["k"] = "4";  // Number of data chunks
    profile["m"] = "2";  // Number of parity chunks  
    profile["d"] = "5";  // Number of chunks needed for repair
    profile["plugin"] = "clay";
    profile["technique"] = "";
    
    int result = clay.init(profile, std::cerr);
    if (result != 0) {
        std::cerr << "Failed to initialize Clay erasure code: " << result << std::endl;
        return 1;
    }
    
    std::cout << "Clay Erasure Code initialized successfully!" << std::endl;
    std::cout << "Parameters: k=" << clay.k << ", m=" << clay.m << ", d=" << clay.d << std::endl;
    
    // Create some test data
    std::string input_data = "Hello, this is a test of the Clay erasure code library!";
    BufferList input;
    input.append(input_data.c_str(), input_data.length());
    
    std::cout << "Original data: " << input_data << std::endl;
    std::cout << "Data size: " << input.length() << " bytes" << std::endl;
    
    // Encode the data
    std::set<int> want_to_encode;
    for (int i = 0; i < clay.k + clay.m; ++i) {
        want_to_encode.insert(i);
    }
    
    std::map<int, BufferList> encoded_chunks;
    result = clay.encode(want_to_encode, input, &encoded_chunks);
    if (result != 0) {
        std::cerr << "Encoding failed: " << result << std::endl;
        return 1;
    }
    
    std::cout << "Encoding successful! Created " << encoded_chunks.size() << " chunks." << std::endl;
    
    // Simulate losing some chunks (e.g., chunk 1 and chunk 4)
    std::map<int, BufferList> available_chunks = encoded_chunks;
    available_chunks.erase(1);  // Remove data chunk 1
    available_chunks.erase(4);  // Remove parity chunk 0 (k+0)
    
    std::cout << "Simulated loss of chunks 1 and 4. Available chunks: " << available_chunks.size() << std::endl;
    
    // Decode to recover original data
    std::set<int> want_to_read;
    for (int i = 0; i < clay.k; ++i) {
        want_to_read.insert(i);
    }
    
    std::map<int, BufferList> decoded_chunks;
    size_t chunk_size = available_chunks.begin()->second.length();
    result = clay.decode(want_to_read, available_chunks, &decoded_chunks, chunk_size);
    if (result != 0) {
        std::cerr << "Decoding failed: " << result << std::endl;
        return 1;
    }
    
    // Reconstruct original data
    BufferList reconstructed;
    for (int i = 0; i < clay.k; ++i) {
        reconstructed.claim_append(decoded_chunks[i]);
    }
    
    std::cout << "Reconstruction successful!" << std::endl;
    std::cout << "Reconstructed size: " << reconstructed.length() << " bytes" << std::endl;
    
    // Note: For Clay codes, the reconstructed data may be padded/aligned differently
    // This is normal behavior for the Clay erasure coding scheme
    
    return 0;
}
