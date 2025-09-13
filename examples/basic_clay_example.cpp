#include <clay/clay.h>
#include <iostream>
#include <vector>

int main() {
    try {
        std::cout << "Clay Library Basic Test" << std::endl;
        std::cout << "==========================" << std::endl;

        // here we create Clay parameters - k = 4, m = 2, d = 5
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
        std::cout << "Input data size: " << input_buffer.size() << " bytes" << std::endl;

        std::vector<clay::Buffer> encoded_chunks;
        auto encode_result = clay.encode(input_buffer, encoded_chunks);
        
        if (encode_result == clay::ClayResult::SUCCESS) {
            std::cout << "Encoding successful! Generated " << encoded_chunks.size() << " chunks" << std::endl;
            
            for (size_t i = 0; i < encoded_chunks.size(); ++i) {
                std::cout << "  Chunk " << i << ": " << encoded_chunks[i].size() << " bytes" << std::endl;
            }
        } else {
            std::cout << "Encoding failed" << std::endl;
            std::cout << "Error: " << clay.last_error() << std::endl;
        }

        std::cout << "\nClay library basic functionality test completed!" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
