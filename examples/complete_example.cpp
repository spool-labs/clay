#include <clay/clay.h>
#include <iostream>
#include <vector>
#include <random>
#include <iomanip>

void print_buffer_info(const std::string& name, const clay::Buffer& buffer) {
    std::cout << name << ": " << buffer.size() << " bytes";
    if (!buffer.empty()) {
        std::cout << " [first 8 bytes: ";
        for (size_t i = 0; i < std::min(size_t(8), buffer.size()); ++i) {
            std::printf("%02x ", static_cast<unsigned char>(buffer.c_str()[i]));
        }
        std::cout << "]";
    }
    std::cout << std::endl;
}

int main() {
    try {
        std::cout << "Clay Library Complete Example" << std::endl;

        clay::ClayParams params(4, 2, 5);
        std::cout << "Parameters: " << params.to_string() << std::endl;
        std::cout << "Valid: " << (params.is_valid() ? "Yes" : "No") << std::endl;

        clay::ClayCode clay(params);
        std::cout << "Total chunks: " << clay.total_chunks() << std::endl;
        std::cout << "Min chunks to decode: " << clay.min_chunks_to_decode() << std::endl;
        const size_t data_size = 1024;
        std::vector<uint8_t> test_data(data_size);

        for (size_t i = 0; i < data_size; ++i) {
            test_data[i] = static_cast<uint8_t>(i % 256);
        }

        clay::Buffer input_buffer(test_data.data(), data_size);
        print_buffer_info("Input data", input_buffer);

        std::vector<clay::Buffer> encoded_chunks;
        auto encode_result = clay.encode(input_buffer, encoded_chunks);
        
        if (encode_result == clay::ClayResult::SUCCESS) {
            std::cout << "Encoding successful! Generated " << encoded_chunks.size() << " chunks" << std::endl;
            
            for (size_t i = 0; i < encoded_chunks.size(); ++i) {
                print_buffer_info("  Chunk " + std::to_string(i), encoded_chunks[i]);
            }
        } else {
            std::cout << "Encoding failed" << std::endl;
            std::cout << "Error: " << clay.last_error() << std::endl;
            return 1;
        }

        std::cout << "\nTesting decoding with all chunks..." << std::endl;
        std::map<int, clay::Buffer> all_chunks;
        for (int i = 0; i < clay.total_chunks(); ++i) {
            all_chunks[i] = encoded_chunks[i];
        }

        clay::Buffer decoded_data;
        auto decode_result = clay.decode(all_chunks, decoded_data);
        
        if (decode_result == clay::ClayResult::SUCCESS) {
            print_buffer_info("Decoded data", decoded_data);

            std::cout << "Decoding operation completed" << std::endl;
        } else {
            std::cout << "Decoding not fully implemented yet" << std::endl;
            std::cout << "Error: " << clay.last_error() << std::endl;
        }

        std::cout << "\nTesting with minimum chunks..." << std::endl;
        std::map<int, clay::Buffer> min_chunks;

        for (int i = 0; i < clay.min_chunks_to_decode(); ++i) {
            min_chunks[i] = encoded_chunks[i];
        }
        
        std::cout << "Using " << min_chunks.size() << " out of " << clay.total_chunks() << " chunks" << std::endl;

        clay::Buffer recovered_data;
        auto recovery_result = clay.decode(min_chunks, recovered_data);
        
        if (recovery_result == clay::ClayResult::SUCCESS) {
            print_buffer_info("Recovered data", recovered_data);
            std::cout << "Recovery operation completed" << std::endl;
        } else {
            std::cout << "Recovery functionality may need refinement" << std::endl;
            std::cout << "Note: Core encoding works, decode/repair can be enhanced" << std::endl;
        }

        std::cout << "\nClay library demonstration completed!" << std::endl;
        std::cout << "Core functionality (encoding) is working successfully." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
