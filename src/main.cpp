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
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

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
                std::cout << std::hex << std::setw(2) << std::setfill('0')
                          << static_cast<int>(static_cast<unsigned char>(data[j])) << " ";
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

// Read input file
int read_input_file(const std::string& input_file, buffer::list& input, size_t& input_size) {
    std::ifstream ifs(input_file, std::ios::binary | std::ios::ate);
    if (!ifs) {
        std::cerr << "ERROR: Cannot open input file: " << input_file << std::endl;
        return 1;
    }
    input_size = ifs.tellg();
    ifs.seekg(0);
    std::vector<char> data(input_size);
    ifs.read(data.data(), input_size);
    ifs.close();

    buffer::ptr input_ptr = buffer::create_aligned(input_size, ErasureCode::SIMD_ALIGN);
    input_ptr.copy_in(0, input_size, data.data());
    input.push_back(std::move(input_ptr));
    std::cout << "Read input file: " << input_file << " (" << input_size << " bytes)" << std::endl;
    return 0;
}

// Write chunk to file
int write_chunk(const std::string& output_dir, int index, const buffer::list& chunk) {
    std::string filename = output_dir + "/chunk_" + std::to_string(index) + ".dat";
    std::ofstream ofs(filename, std::ios::binary);
    if (!ofs) {
        std::cerr << "ERROR: Cannot open output file: " << filename << std::endl;
        return 1;
    }
    std::vector<char> data(chunk.length());
    buffer::list_iterator iter = chunk.begin();
    iter.copy(chunk.length(), data.data());
    ofs.write(data.data(), chunk.length());
    ofs.close();
    return 0;
}

// Read chunk from file
int read_chunk(const std::string& output_dir, int index, buffer::list& chunk) {
    std::string filename = output_dir + "/chunk_" + std::to_string(index) + ".dat";
    std::ifstream ifs(filename, std::ios::binary | std::ios::ate);
    if (!ifs) {
        return 1; // Missing chunk is valid in repair mode
    }
    size_t size = ifs.tellg();
    ifs.seekg(0);
    std::vector<char> data(size);
    ifs.read(data.data(), size);
    ifs.close();

    buffer::ptr chunk_ptr = buffer::create_aligned(size, ErasureCode::SIMD_ALIGN);
    chunk_ptr.copy_in(0, size, data.data());
    chunk.push_back(std::move(chunk_ptr));
    return 0;
}

// Write metadata file
int write_metadata(const std::string& output_dir, size_t input_size) {
    std::string filename = output_dir + "/metadata.txt";
    std::ofstream ofs(filename);
    if (!ofs) {
        std::cerr << "ERROR: Cannot open metadata file: " << filename << std::endl;
        return 1;
    }
    ofs << "input_size=" << input_size << std::endl;
    ofs.close();
    std::cout << "Metadata written to: " << filename << std::endl;
    return 0;
}

// Read metadata file
int read_metadata(const std::string& output_dir, size_t& input_size) {
    std::string filename = output_dir + "/metadata.txt";
    std::ifstream ifs(filename);
    if (!ifs) {
        std::cerr << "ERROR: Cannot open metadata file: " << filename << std::endl;
        return 1;
    }
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.find("input_size=") == 0) {
            input_size = std::stoull(line.substr(11));
            std::cout << "Read metadata: input_size=" << input_size << std::endl;
            ifs.close();
            return 0;
        }
    }
    ifs.close();
    std::cerr << "ERROR: input_size not found in metadata file" << std::endl;
    return 1;
}

// Encode mode
int encode_mode(ErasureCodeClay& clay, const std::string& input_file, const std::string& output_dir) {
    // Read input file
    buffer::list input;
    size_t input_size;
    if (read_input_file(input_file, input, input_size) != 0) {
        return 1;
    }

    // Setup input size
    size_t chunk_size = clay.get_chunk_size(input_size);
    size_t padded_size = chunk_size * clay.k;
    std::cout << "Computed chunk size: " << chunk_size << " bytes" << std::endl;
    std::cout << "Padded size for " << clay.k << " data chunks: " << padded_size << " bytes" << std::endl;

    // Encode data
    std::map<int, buffer::list> encoded;
    std::set<int> want_to_encode;
    for (int i = 0; i < static_cast<int>(clay.get_chunk_count()); ++i) {
        want_to_encode.insert(i);
    }
    int result = clay.encode(want_to_encode, input, &encoded);
    if (result != 0) {
        std::cerr << "ERROR: Encoding failed with code: " << result << std::endl;
        return result;
    }
    print_chunk_hex(encoded, clay.k, clay.m, "After Encoding");

    // Write chunks to files
    fs::create_directories(output_dir);
    for (const auto& [index, chunk] : encoded) {
        if (write_chunk(output_dir, index, chunk) != 0) {
            return 1;
        }
    }

    // Write metadata
    if (write_metadata(output_dir, input_size) != 0) {
        return 1;
    }

    std::cout << "Encoded chunks written to: " << output_dir << std::endl;
    return 0;
}

// Decode mode
int decode_mode(ErasureCodeClay& clay, const std::string& output_dir, const std::string& output_file) {
    // Read metadata to get original input size
    size_t input_size;
    if (read_metadata(output_dir, input_size) != 0) {
        return 1;
    }

    // Read available chunks
    std::map<int, buffer::list> available_chunks;
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        buffer::list chunk;
        if (read_chunk(output_dir, i, chunk) == 0) {
            available_chunks[i] = chunk;
        }
    }
    if (available_chunks.size() < static_cast<size_t>(clay.k)) {
        std::cerr << "ERROR: Not enough chunks (" << available_chunks.size()
                  << ") to reconstruct data (need " << clay.k << ")" << std::endl;
        return 1;
    }
    print_chunk_hex(available_chunks, clay.k, clay.m, "Available Chunks");

    // Decode data
    std::map<int, buffer::list> decoded_data;
    std::set<int> want_to_read;
    for (int i = 0; i < clay.k; ++i) {
        want_to_read.insert(i);
    }
    size_t chunk_size = available_chunks.begin()->second.length();
    int result = clay.decode(want_to_read, available_chunks, &decoded_data, chunk_size);
    if (result != 0) {
        std::cerr << "ERROR: Decoding failed with code: " << result << std::endl;
        return result;
    }

    // Write reconstructed data to output file, trimming to original input size
    buffer::list reconstructed;
    for (int i = 0; i < clay.k; ++i) {
        reconstructed.append(decoded_data.at(i));
    }
    std::ofstream ofs(output_file, std::ios::binary);
    if (!ofs) {
        std::cerr << "ERROR: Cannot open output file: " << output_file << std::endl;
        return 1;
    }
    std::vector<char> data(input_size); // Use original input size
    buffer::list_iterator iter = reconstructed.begin();
    iter.copy(input_size, data.data()); // Copy only up to input_size
    ofs.write(data.data(), input_size);
    ofs.close();
    std::cout << "Reconstructed data written to: " << output_file << " (" << input_size << " bytes)" << std::endl;
    return 0;
}

// Repair mode
int repair_mode(ErasureCodeClay& clay, const std::string& output_dir) {
    // Read available chunks
    std::map<int, buffer::list> available_chunks;
    std::set<int> want_to_read;
    size_t chunk_size = 0;
    for (int i = 0; i < clay.get_chunk_count(); ++i) {
        buffer::list chunk;
        if (read_chunk(output_dir, i, chunk) == 0) {
            available_chunks[i] = chunk;
            if (chunk_size == 0) chunk_size = chunk.length();
        } else {
            want_to_read.insert(i);
        }
    }
    if (available_chunks.size() < static_cast<size_t>(clay.k)) {
        std::cerr << "ERROR: Not enough chunks (" << available_chunks.size()
                  << ") to repair data (need " << clay.k << ")" << std::endl;
        return 1;
    }
    print_chunk_hex(available_chunks, clay.k, clay.m, "Before Repair");

    // Repair chunks
    std::map<int, buffer::list> repaired;
    int result = clay.decode(want_to_read, available_chunks, &repaired, chunk_size);
    if (result != 0) {
        std::cerr << "ERROR: Repair failed with code: " << result << std::endl;
        return result;
    }

    // Write repaired chunks
    for (const auto& [index, chunk] : repaired) {
        if (write_chunk(output_dir, index, chunk) != 0) {
            return 1;
        }
        std::cout << "Repaired chunk " << index << " written to: " << output_dir << std::endl;
    }
    print_chunk_hex(repaired, clay.k, clay.m, "Repaired Chunks");
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <mode:encode|decode|repair> <input_file/output_dir> <output_dir/output_file> [k=8] [m=4] [d=11]" << std::endl;
        return 1;
    }

    std::string mode = argv[1];
    std::string input_path = argv[2];
    std::string output_path = argv[3];

    // Default parameters
    int k = 8, m = 4, d = 11;
    if (argc > 4) k = std::stoi(argv[4]);
    if (argc > 5) m = std::stoi(argv[5]);
    if (argc > 6) d = std::stoi(argv[6]);

    // Initialize CLAY
    ErasureCodeClay clay(".");
    ErasureCodeProfile profile;
    profile["k"] = std::to_string(k);
    profile["m"] = std::to_string(m);
    profile["d"] = std::to_string(d);
    profile["scalar_mds"] = "jerasure";
    profile["technique"] = "reed_sol_van";
    std::ostringstream oss;
    if (initialize_clay(clay, profile, oss) != 0) {
        return 1;
    }

    // Execute requested mode
    if (mode == "encode") {
        return encode_mode(clay, input_path, output_path);
    } else if (mode == "decode") {
        return decode_mode(clay, input_path, output_path);
    } else if (mode == "repair") {
        return repair_mode(clay, input_path);
    } else {
        std::cerr << "ERROR: Invalid mode. Use 'encode', 'decode', or 'repair'" << std::endl;
        return 1;
    }
}
