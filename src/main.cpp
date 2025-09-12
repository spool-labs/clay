#include "ErasureCodeInterface.h"
#include "ErasureCodeClay.h"
#include "BufferList.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <string>
#include <random>
#include <map>
#include <set>
#include <cassert>
#include <vector>
#include <algorithm>

// Function to print the first 20 bytes of each chunk in hex
void print_chunk_hex(const std::map<int, buffer::list>& chunks, int k, int m, const std::string& stage) {
    std::cout << "Chunk contents at stage: " << stage << std::endl;
    const size_t bytes_to_print = 20;
    for (int i = 0; i < k + m; ++i) {
        std::string label = (i < k) ? "D" + std::to_string(i) : "C" + std::to_string(i - k);
        auto it = chunks.find(i);
        if (it != chunks.end()) {
            const buffer::list& chunk = it->second;
            std::vector<char> data(bytes_to_print);
            buffer::list_iterator iter = chunk.begin();
            size_t to_copy = std::min<size_t>(chunk.length(), bytes_to_print);
            iter.copy(to_copy, data.data());
            std::cout << "  Chunk " << label << ": ";
            for (size_t j = 0; j < to_copy; ++j) {
                unsigned char byte = static_cast<unsigned char>(data[j]);
                std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte) << " ";
            }
            if (chunk.length() > bytes_to_print) {
                std::cout << "...";
            }
            std::cout << std::dec << std::endl;
        } else {
            std::cout << "  Chunk " << label << ": [ERASED]" << std::endl;
        }
    }
    std::cout << std::endl;
}

// Initialize CLAY with given profile
int initialize_clay(ErasureCodeClay& clay, ErasureCodeProfile& profile, std::ostringstream& oss) {
    std::cout << "Configuring CLAY with k=" << profile["k"] << ", m=" << profile["m"]
              << ", d=" << profile["d"] << ", scalar_mds=" << profile["scalar_mds"]
              << ", technique=" << profile["technique"] << std::endl;
    int result = clay.init(profile, &oss);
    if (result != 0) {
        std::cerr << "ERROR: Failed to initialize CLAY: " << oss.str() << std::endl;
        return result;
    }
    std::cout << "CLAY initialized successfully" << std::endl;
    return 0;
}

// Set up and verify input size alignment
int setup_input_size(ErasureCodeClay& clay, size_t input_size, int w, size_t& chunk_size, size_t& padded_size) {
    std::cout << "Input size set to " << input_size << " bytes (1MB)" << std::endl;
    chunk_size = clay.get_chunk_size(input_size);
    padded_size = chunk_size * clay.k;
    std::cout << "Computed chunk size: " << chunk_size << " bytes" << std::endl;
    std::cout << "Padded size for " << clay.k << " data chunks: " << padded_size << " bytes" << std::endl;

    if (padded_size % (clay.k * (w / 8)) != 0 || padded_size % ErasureCode::SIMD_ALIGN != 0) {
        std::cerr << "ERROR: Padded size (" << padded_size << ") is not aligned with "
                  << (clay.k * (w / 8)) << " or " << ErasureCode::SIMD_ALIGN << std::endl;
        return 1;
    }
    std::cout << "Padded size alignment verified" << std::endl;
    return 0;
}

// Generate random input data
int generate_input_data(size_t input_size, size_t padded_size, buffer::list& input) {
    std::cout << "Generating random input data..." << std::endl;
    std::string input_data(padded_size, '\0');
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (size_t i = 0; i < input_size; ++i) {
        input_data[i] = static_cast<char>(dis(gen));
    }
    std::cout << "Created input data: " << input_size << " bytes of random data, padded to "
              << padded_size << " bytes" << std::endl;

    buffer::ptr input_ptr = buffer::create_aligned(padded_size, ErasureCode::SIMD_ALIGN);
    input_ptr.copy_in(0, padded_size, input_data.c_str());
    input.push_back(std::move(input_ptr));
    std::cout << "Input bufferlist created with length: " << input.length() << " bytes" << std::endl;
    return 0;
}

// Encode the input data
int encode_data(ErasureCodeClay& clay, const buffer::list& input, std::map<int, buffer::list>& encoded) {
    std::cout << "Encoding data into " << clay.get_chunk_count() << " chunks (k=" << clay.k
              << ", m=" << clay.m << ")..." << std::endl;
    std::set<int> want_to_encode;
    for (int i = 0; i < static_cast<int>(clay.get_chunk_count()); ++i) {
        want_to_encode.insert(i);
    }
    int result = clay.encode(want_to_encode, input, &encoded);
    if (result != 0) {
        std::cerr << "ERROR: Encoding failed with code: " << result << std::endl;
        return result;
    }
    std::cout << "Encoding completed. Generated " << encoded.size() << " chunks:" << std::endl;
    for (const auto& [index, chunk] : encoded) {
        std::cout << "  Chunk " << index << ": " << chunk.length() << " bytes" << std::endl;
    }
    return 0;
}

// Simulate loss of a random number of chunks (up to m)
std::set<int> simulate_chunk_loss(std::map<int, buffer::list>& available_chunks, int k, int m) {
    std::random_device rd;
    std::mt19937 gen(rd());
    // Randomly choose number of chunks to erase (1 to m)
    std::uniform_int_distribution<> num_dis(1, m);
    int num_to_erase = num_dis(gen);
    std::cout << "Simulating loss of " << num_to_erase << " chunks..." << std::endl;

    // Generate a list of all possible chunk indices
    std::vector<int> indices(k + m);
    std::iota(indices.begin(), indices.end(), 0);
    // Shuffle indices
    std::shuffle(indices.begin(), indices.end(), gen);
    // Select the first num_to_erase indices to erase
    std::set<int> erased_chunks;
    for (int i = 0; i < num_to_erase; ++i) {
        int erased_chunk = indices[i];
        erased_chunks.insert(erased_chunk);
        available_chunks.erase(erased_chunk);
        std::cout << "  Chunk " << erased_chunk << " erased." << std::endl;
    }
    std::cout << "Total available chunks after erasure: " << available_chunks.size() << std::endl;
    return erased_chunks;
}

// Determine minimum chunks needed for repair
int determine_minimum_chunks(ErasureCodeClay& clay, const std::set<int>& want_to_read,
                            const std::map<int, buffer::list>& available_chunks,
                            std::map<int, std::vector<std::pair<int, int>>>& minimum,
                            const std::string& d) {
    std::cout << "Determining minimum chunks to repair chunks {";
    for (int i : want_to_read) std::cout << i << " ";
    std::cout << "} (d=" << d << ")..." << std::endl;
    std::set<int> available;
    for (const auto& [index, _] : available_chunks) {
        available.insert(index);
    }
    std::cout << "Available chunks for repair: ";
    for (int i : available) {
        std::cout << i << " ";
    }
    std::cout << std::endl;

    // Log repair sub-chunk count
    int repair_sub_chunk_count = clay.get_repair_sub_chunk_count(want_to_read);
    std::cout << "Number of sub-chunks needed for repair: " << repair_sub_chunk_count << std::endl;

    int result = clay.minimum_to_decode(want_to_read, available, &minimum);
    if (result != 0) {
        std::cerr << "ERROR: Failed to determine minimum chunks for repair: " << result << std::endl;
        return result;
    }

    return 0;
}

// Repair the lost chunks
int repair_chunk(ErasureCodeClay& clay, const std::set<int>& want_to_read,
                 const std::map<int, buffer::list>& available_chunks,
                 std::map<int, buffer::list>& repaired, size_t chunk_size) {
    std::cout << "Repairing chunks {";
    for (int i : want_to_read) std::cout << i << " ";
    std::cout << "}..." << std::endl;
    int result = clay.decode(want_to_read, available_chunks, &repaired, chunk_size);
    if (result != 0) {
        std::cerr << "ERROR: Repair failed with code: " << result << std::endl;
        return result;
    }
    for (int i : want_to_read) {
        std::cout << "Chunk " << i << " repaired successfully. Repaired chunk size: "
                  << repaired[i].length() << " bytes" << std::endl;
    }
    return 0;
}

// Verify the repaired chunks
int verify_repaired_chunk(const std::map<int, buffer::list>& repaired,
                          const std::map<int, buffer::list>& encoded, const std::set<int>& erased_chunks) {
    for (int erased_chunk : erased_chunks) {
        std::cout << "Verifying repaired chunk " << erased_chunk << " against original chunk " << erased_chunk << "..." << std::endl;
        if (repaired.find(erased_chunk) == repaired.end()) {
            std::cerr << "ERROR: Repaired chunk " << erased_chunk << " not found in repaired set" << std::endl;
            return 1;
        }
        size_t len = repaired.at(erased_chunk).length();
        if (len != encoded.at(erased_chunk).length()) {
            std::cerr << "ERROR: Repaired chunk " << erased_chunk << " length (" << len
                      << ") does not match original (" << encoded.at(erased_chunk).length() << ")" << std::endl;
            return 1;
        }

        std::vector<char> repaired_data(len);
        std::vector<char> original_data(len);
        buffer::list_iterator repaired_iter = repaired.at(erased_chunk).begin();
        buffer::list_iterator original_iter = encoded.at(erased_chunk).begin();
        repaired_iter.copy(len, repaired_data.data());
        original_iter.copy(len, original_data.data());

        if (std::memcmp(repaired_data.data(), original_data.data(), len) == 0) {
            std::cout << "SUCCESS: Repaired chunk " << erased_chunk << " matches original chunk " << erased_chunk << std::endl;
        } else {
            std::cerr << "ERROR: Repaired chunk " << erased_chunk << " does not match original" << std::endl;
            return 1;
        }
    }
    return 0;
}

// Reconstruct the original data
int reconstruct_data(ErasureCodeClay& clay, const std::map<int, buffer::list>& available_chunks,
                     size_t chunk_size, std::map<int, buffer::list>& decoded_data) {
    std::cout << "Reconstructing original data from data chunks..." << std::endl;
    std::set<int> data_want_to_read;
    for (int i = 0; i < clay.k; ++i) {
        data_want_to_read.insert(i);
    }
    int result = clay.decode(data_want_to_read, available_chunks, &decoded_data, chunk_size);
    if (result != 0) {
        std::cerr << "ERROR: Decoding data chunks failed with code: " << result << std::endl;
        return result;
    }
    std::cout << "Data chunks decoded: " << decoded_data.size() << " chunks" << std::endl;
    return 0;
}

// Concatenate and verify reconstructed data
int verify_reconstructed_data(const std::map<int, buffer::list>& decoded_data,
                             const buffer::list& input, size_t input_size, int k) {
    std::cout << "Concatenating data chunks for reconstruction..." << std::endl;
    buffer::list reconstructed;
    size_t total_size = 0;
    for (int i = 0; i < k; ++i) {
        reconstructed.append(decoded_data.at(i));
        total_size += decoded_data.at(i).length();
    }
    std::cout << "Reconstruction completed. Reconstructed data size: " << total_size << " bytes" << std::endl;

    std::cout << "Verifying reconstructed data against original input..." << std::endl;
    if (total_size < input_size) {
        std::cerr << "ERROR: Reconstructed data size (" << total_size << ") is less than input size ("
                  << input_size << ")" << std::endl;
        return 1;
    }

    std::vector<char> reconstructed_data(input_size);
    std::vector<char> input_data(input_size);
    buffer::list_iterator reconstructed_iter = reconstructed.begin();
    buffer::list_iterator input_iter = input.begin();
    reconstructed_iter.copy(input_size, reconstructed_data.data());
    input_iter.copy(input_size, input_data.data());

    if (std::memcmp(reconstructed_data.data(), input_data.data(), input_size) == 0) {
        std::cout << "SUCCESS: Reconstructed data matches original input" << std::endl;
        return 0;
    } else {
        std::cerr << "ERROR: Reconstructed data does not match original input" << std::endl;
        return 1;
    }
}

int main() {
    std::cout << "Starting CLAY erasure coding test with k=8, m=4, d=11..." << std::endl;

    // Initialize CLAY
    ErasureCodeClay clay(".");
    ErasureCodeProfile profile;
    profile["k"] = "8";
    profile["m"] = "4";
    profile["d"] = "11";
    profile["scalar_mds"] = "jerasure";
    profile["technique"] = "reed_sol_van";
    std::ostringstream oss;
    if (initialize_clay(clay, profile, oss) != 0) {
        return 1;
    }

    // Set input size and verify alignment
    const size_t input_size = 1024 * 1024; // 1MB
    const int w = 8; // Word size
    size_t chunk_size, padded_size;
    if (setup_input_size(clay, input_size, w, chunk_size, padded_size) != 0) {
        return 1;
    }

    // Generate input data
    buffer::list input;
    if (generate_input_data(input_size, padded_size, input) != 0) {
        return 1;
    }

    // Encode data
    std::map<int, buffer::list> encoded;
    if (encode_data(clay, input, encoded) != 0) {
        return 1;
    }
    print_chunk_hex(encoded, clay.k, clay.m, "After Encoding");

    // Simulate random chunk loss
    std::map<int, buffer::list> available_chunks = encoded;
    std::set<int> erased_chunks = simulate_chunk_loss(available_chunks, clay.k, clay.m);
    std::string stage = "After Erasing Chunks {";
    for (int i : erased_chunks) stage += std::to_string(i) + " ";
    stage += "}";
    print_chunk_hex(available_chunks, clay.k, clay.m, stage);

    // Determine minimum chunks for repair
    std::map<int, std::vector<std::pair<int, int>>> minimum;
    if (determine_minimum_chunks(clay, erased_chunks, available_chunks, minimum, profile["d"]) != 0) {
        return 1;
    }

    // Log CLAY's bandwidth savings
    int repair_sub_chunk_count = clay.get_repair_sub_chunk_count(erased_chunks);
    size_t sub_chunk_size = chunk_size / clay.get_sub_chunk_count(); // Assumes sub-chunks are evenly divided
    size_t total_repair_data = repair_sub_chunk_count * sub_chunk_size;
    size_t per_chunk_data = total_repair_data / std::stoi(profile["d"]); // Data from each of d helper chunks
    std::cout << "Estimated repair bandwidth: " << total_repair_data << " bytes total ("
              << per_chunk_data << " bytes per chunk from d=" << profile["d"] << " chunks)" << std::endl;

    // Repair chunks
    std::map<int, buffer::list> repaired;
    if (repair_chunk(clay, erased_chunks, available_chunks, repaired, chunk_size) != 0) {
        return 1;
    }

    // Verify repaired chunks
    if (verify_repaired_chunk(repaired, encoded, erased_chunks) != 0) {
        return 1;
    }

    // Add repaired chunks back
    for (int erased_chunk : erased_chunks) {
        available_chunks[erased_chunk] = std::move(repaired[erased_chunk]);
        std::cout << "Repaired chunk " << erased_chunk << " added back." << std::endl;
    }
    std::cout << "Total available chunks after repair: " << available_chunks.size() << std::endl;
    stage = "After Repairing Chunks {";
    for (int i : erased_chunks) stage += std::to_string(i) + " ";
    stage += "}";
    print_chunk_hex(available_chunks, clay.k, clay.m, stage);

    // Reconstruct and verify data
    std::map<int, buffer::list> decoded_data;
    if (reconstruct_data(clay, available_chunks, chunk_size, decoded_data) != 0) {
        return 1;
    }
    if (verify_reconstructed_data(decoded_data, input, input_size, clay.k) != 0) {
        return 1;
    }

    std::cout << "CLAY erasure coding test with random chunk loss completed successfully" << std::endl;
    return 0;
}
