#include "ErasureCodeInterface.h"
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

void print_data_and_coding(int k, int m, int w, size_t size, const std::map<int, buffer::list>& chunks);

int main() {

    ErasureCodeClay clay(".");
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

    // Set input size and verify alignment
    const size_t input_size = 1024; // 1024 bytes for testing
    int w = 8; // Default word size
    size_t chunk_size = clay.get_chunk_size(input_size); // Use CLAY's chunk size
    std::cout << "Computed chunk size: " << chunk_size << " bytes" << std::endl;

    // Generate sample input data with padding
    size_t padded_size = chunk_size * clay.k; // 256 * 4 = 1024 bytes
    if (padded_size % (clay.k * (w / 8)) != 0 || padded_size % ErasureCode::SIMD_ALIGN != 0) {
        std::cerr << "Padded size (" << padded_size << ") must be a multiple of "
                  << (clay.k * (w / 8)) << " and " << ErasureCode::SIMD_ALIGN << std::endl;
        return 1;
    }

    // Create input data
    std::string input_data(padded_size, '\0');
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < input_size; ++i) {
        input_data[i] = static_cast<char>(dis(gen)); // Random bytes for actual data
    }

    // Remaining bytes are already zero (padding)
    buffer::ptr input_ptr = buffer::create_aligned(padded_size, ErasureCode::SIMD_ALIGN);
    input_ptr.copy_in(0, padded_size, input_data.c_str());
    buffer::list input;
    input.push_back(std::move(input_ptr));
    std::cout << "Input bufferlist length: " << input.length() << " bytes" << std::endl;

    // Encode the data
    std::set<int> want_to_encode;
    for (int i = 0; i < static_cast<int>(clay.get_chunk_count()); ++i) {
        want_to_encode.insert(i);
    }
    std::map<int, buffer::list> encoded;
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
    print_data_and_coding(clay.k, clay.m, w, chunk_size, encoded);
    std::cout << std::endl;

    // Simulate loss of one chunk (e.g., chunk 0)
    std::map<int, buffer::list> available_chunks = encoded;
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
    std::map<int, buffer::list> repaired;
    int repair_result = clay.decode(want_to_read, available_chunks, &repaired, chunk_size);
    if (repair_result != 0) {
        std::cerr << "Repair failed: " << repair_result << std::endl;
        return 1;
    }
    std::cout << "Repaired chunk 0" << std::endl;

    // Add repaired chunk back to available_chunks for full reconstruction
    available_chunks[0] = std::move(repaired[0]);

    // Print data and coding after repair
    print_data_and_coding(clay.k, clay.m, w, chunk_size, available_chunks);
    std::cout << std::endl;

    // Reconstruct the original data (manual concat of data chunks)
    std::set<int> data_want_to_read;
    for (int i = 0; i < clay.k; ++i) { // k=4 data chunks
        data_want_to_read.insert(i);
    }
    std::map<int, buffer::list> decoded_data;
    int decode_result = clay.decode(data_want_to_read, available_chunks, &decoded_data, chunk_size);
    if (decode_result != 0) {
        std::cerr << "Decoding data chunks failed: " << decode_result << std::endl;
        return 1;
    }

    // Concat the k data chunks into reconstructed
    buffer::list reconstructed;
    size_t total_size = 0;
    for (int i = 0; i < clay.k; ++i) {
        reconstructed.append(decoded_data[i]); // Appends without clearing source
        total_size += decoded_data[i].length();
    }
    std::cout << "Reconstructed data size: " << total_size << " bytes" << std::endl;

    // Verify the reconstructed data (only compare original input_size)
    if (total_size >= input_size &&
        std::memcmp(reconstructed.c_str(), input.c_str(), input_size) == 0) {
        std::cout << "Success: Reconstructed data matches original input" << std::endl;
    } else {
        std::cerr << "Error: Reconstructed data does not match original input" << std::endl;
        return 1;
    }

    return 0;
}

void print_data_and_coding(int k, int m, int w, size_t size, const std::map<int, buffer::list>& chunks) {
    int n = (k > m) ? k : m;
    int bytes_per_group = w / 8; // e.g., 1 for w=8
    int max_bytes_to_print = 8;  // Print only first 8 bytes for brevity

    for (int i = 0; i < n; ++i) {
        if (i < k) {
            std::cout << "D" << std::setw(2) << i << ":";
            auto it = chunks.find(i);
            if (it != chunks.end()) {
                const buffer::list& data_chunk = it->second;
                assert(data_chunk.length() == size);
                // Copy data using iterator to avoid non-const c_str()
                std::vector<char> data(max_bytes_to_print);
                buffer::list_iterator iter = data_chunk.begin();
                iter.copy(std::min(size, (size_t)max_bytes_to_print), data.data());
                for (size_t j = 0; j < std::min(size, (size_t)max_bytes_to_print); j += bytes_per_group) {
                    std::cout << " ";
                    for (int x = 0; x < bytes_per_group; ++x) {
                        if (j + x < (size_t)max_bytes_to_print) {
                            unsigned char byte = static_cast<unsigned char>(data[j + x]);
                            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
                        }
                    }
                }
                if (size > max_bytes_to_print) std::cout << "..."; // Indicate truncation
                std::cout << std::setw(4) << " ";
            } else {
                std::cout << "[ERASED]" << std::setw(4) << " ";
            }
        }

        if (i < m) {
            // Print Coding chunk
            int coding_idx = k + i;
            auto it = chunks.find(coding_idx);
            std::cout << "C" << std::setw(2) << i << ":";
            if (it != chunks.end()) {
                const buffer::list& coding_chunk = it->second;
                assert(coding_chunk.length() == size);
                // Copy data using iterator to avoid non-const c_str()
                std::vector<char> data(max_bytes_to_print);
                buffer::list_iterator iter = coding_chunk.begin();
                iter.copy(std::min(size, (size_t)max_bytes_to_print), data.data());
                for (size_t j = 0; j < std::min(size, (size_t)max_bytes_to_print); j += bytes_per_group) {
                    std::cout << " ";
                    for (int x = 0; x < bytes_per_group; ++x) {
                        if (j + x < (size_t)max_bytes_to_print) {
                            unsigned char byte = static_cast<unsigned char>(data[j + x]);
                            std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
                        }
                    }
                }
                if (size > max_bytes_to_print) std::cout << "..."; // Indicate truncation
            } else {
                std::cout << "[ERASED]";
            }
        }
        std::cout << std::dec << std::endl; // Reset hex
    }
    std::cout << std::endl;
}
