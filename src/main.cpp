#include "ErasureCodeInterface.h" // Added for ErasureCodeProfile
#include "ErasureCodeClay.h"
#include "BufferList.h"
#include <iostream>
#include <sstream>
#include <string>
#include <random>
#include <iomanip>
#include <map>
#include <set>
#include <cassert>
#include <vector>

void print_data_and_coding(int k, int m, int w, size_t size, const std::map<int, BufferList>& chunks);

int main() {

    // Initialize the CLAY erasure code
    ErasureCodeClay clay;
    ErasureCodeProfile profile;
    profile["k"] = "4"; // Number of data chunks
    profile["m"] = "2"; // Number of coding chunks
    profile["d"] = "5"; // Repair parameter (k <= d <= k+m-1)
    profile["scalar_mds"] = "jerasure";
    profile["technique"] = "reed_sol_van";

    std::ostringstream oss;
    int init_result = clay.init(profile, &oss);
    if (init_result != 0) {
        std::cerr << "Failed to initialize CLAY: " << oss.str() << std::endl;
        return 1;
    }
    std::cout << "CLAY initialized with profile: " << profile << std::endl;

    // Generate sample input data
    const size_t input_size = 4096; // 4KB input
    std::string input_data(input_size, '\0');
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < input_size; ++i) {
        input_data[i] = static_cast<char>(dis(gen)); // Random bytes
    }

    // Create input BufferList
    BufferList input(input_data.c_str(), input_size, ErasureCode::SIMD_ALIGN);

    // Encode the data
    std::set<int> want_to_encode;
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        want_to_encode.insert(i);
    }
    std::map<int, BufferList> encoded;
    int encode_result = clay.encode(want_to_encode, input, &encoded);
    if (encode_result != 0) {
        std::cerr << "Encoding failed: " << encode_result << std::endl;
        return 1;
    }
    std::cout << "Encoded data into " << encoded.size() << " chunks" << std::endl;

    // Print chunk sizes
    for (const auto& [index, chunk] : encoded) {
        std::cout << "Chunk " << index << " size: " << chunk.length() << " bytes" << std::endl;
    }

    // Print data and coding after encoding
    size_t chunk_size = encoded.begin()->second.length();
    int w = 8; // Default from profile; adjust if needed
    print_data_and_coding(clay.k, clay.m, w, chunk_size, encoded);
    std::cout << std::endl;

    // Simulate loss of one chunk (e.g., chunk 0)
    std::map<int, BufferList> available_chunks = encoded;
    available_chunks.erase(0); // Simulate loss of data chunk 0
    std::cout << "Simulated loss of chunk 0. Available chunks: " << available_chunks.size() << std::endl;

    // Print data and coding after erasure simulation
    print_data_and_coding(clay.k, clay.m, w, chunk_size, available_chunks);
    std::cout << std::endl;

    // Determine minimum chunks needed for repair
    std::set<int> want_to_read = {0}; // We want to recover chunk 0
    std::set<int> available;
    for (const auto& [index, _] : available_chunks) {
        available.insert(index);
    }
    std::map<int, std::vector<std::pair<int, int>>> minimum;
    int min_result = clay.minimum_to_decode(want_to_read, available, &minimum);
    if (min_result != 0) {
        std::cerr << "Failed to determine minimum chunks for repair: " << min_result << std::endl;
        return 1;
    }
    std::cout << "Minimum chunks required for repair: " << minimum.size() << std::endl;

    // Repair the lost chunk
    std::map<int, BufferList> repaired;
    int repair_result = clay.decode(want_to_read, available_chunks, &repaired, chunk_size);
    if (repair_result != 0) {
        std::cerr << "Repair failed: " << repair_result << std::endl;
        return 1;
    }
    std::cout << "Repaired chunk 0" << std::endl;

    // Add repaired chunk back to available_chunks for full reconstruction
    available_chunks[0] = repaired[0];

    // Print data and coding after repair
    print_data_and_coding(clay.k, clay.m, w, chunk_size, available_chunks);
    std::cout << std::endl;

    // Reconstruct the original data (manual concat of data chunks)
    std::set<int> data_want_to_read;
    for (int i = 0; i < clay.k; ++i) {  // k=4 data chunks
        data_want_to_read.insert(i);
    }
    std::map<int, BufferList> decoded_data;
    int decode_result = clay.decode(data_want_to_read, available_chunks, &decoded_data, chunk_size);
    if (decode_result != 0) {
        std::cerr << "Decoding data chunks failed: " << decode_result << std::endl;
        return 1;
    }

    // Concat the k data chunks into reconstructed
    BufferList reconstructed;
    size_t total_size = 0;
    for (int i = 0; i < clay.k; ++i) {
        reconstructed.claim_append(decoded_data[i]);  // Appends and clears source
        total_size += decoded_data[i].length();
    }
    std::cout << "Reconstructed data size: " << total_size << " bytes" << std::endl;

    // Verify the reconstructed data
    if (total_size == input_size &&
        std::memcmp(reconstructed.c_str(), input.c_str(), input_size) == 0) {
        std::cout << "Success: Reconstructed data matches original input" << std::endl;
    } else {
        std::cerr << "Error: Reconstructed data does not match original input" << std::endl;
        return 1;
    }

    return 0;
}

void print_data_and_coding(int k, int m, int w, size_t size, const std::map<int, BufferList>& chunks) {
    int n = (k > m) ? k : m;
    int bytes_per_group = w / 8;  // e.g., 1 for w=8
    int sp = static_cast<int>(size * 2 + size / bytes_per_group + 8);  // Spacing for alignment

    std::cout << std::setw(sp) << "Data" << "Coding" << std::endl;
    for (int i = 0; i < n; ++i) {
        if (i < k) {
            // Print Data chunk
            std::cout << "D" << std::setw(2) << i << ":";
            auto it = chunks.find(i);
            if (it != chunks.end()) {
                const BufferList& data_chunk = it->second;
                assert(data_chunk.length() == size);
                for (size_t j = 0; j < size; j += bytes_per_group) {
                    std::cout << " ";
                    for (int x = 0; x < bytes_per_group; ++x) {
                        unsigned char byte = static_cast<unsigned char>(data_chunk.c_str()[j + x]);
                        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
                    }
                }
                std::cout << std::setw(4) << " ";  // Extra space before coding
            } else {
                // If data chunk missing, print zeros or note
                std::cout << "D" << std::setw(2) << i << ": [ERASED]";
                for (size_t j = 0; j < size / bytes_per_group; ++j) {
                    std::cout << " 00";
                }
                std::cout << std::setw(4) << " ";
            }
        } else {
            std::cout << std::string(sp, ' ');  // Empty space for data column
        }

        if (i < m) {
            // Print Coding chunk (parity i)
            int coding_idx = k + i;
            auto it = chunks.find(coding_idx);
            if (it != chunks.end()) {
                std::cout << "C" << std::setw(2) << i << ":";
                const BufferList& coding_chunk = it->second;
                assert(coding_chunk.length() == size);
                for (size_t j = 0; j < size; j += bytes_per_group) {
                    std::cout << " ";
                    for (int x = 0; x < bytes_per_group; ++x) {
                        unsigned char byte = static_cast<unsigned char>(coding_chunk.c_str()[j + x]);
                        std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
                    }
                }
            } else {
                // If coding chunk missing (e.g., after erasure), print zeros or note
                std::cout << "C" << std::setw(2) << i << ": [ERASED]";
                for (size_t j = 0; j < size / bytes_per_group; ++j) {
                    std::cout << " 00";
                }
            }
        }
        std::cout << std::dec << std::endl;  // Reset hex
    }
    std::cout << std::endl;
}
