#include "ErasureCodeClay.h"
#include "BufferList.h"
#include <iostream>
#include <sstream>
#include <memory>
#include <vector>
#include <chrono>

class ClayWrapper {
private:
    std::unique_ptr<ErasureCodeClay> clay_;
    ErasureCodeProfile profile_;
    int k_, m_, d_;
    bool initialized_;

public:
    ClayWrapper(int k = 4, int m = 2, int d = 5) 
        : k_(k), m_(m), d_(d), initialized_(false) {
        clay_ = std::make_unique<ErasureCodeClay>(".");
        
        profile_["k"] = std::to_string(k_);
        profile_["m"] = std::to_string(m_);
        profile_["d"] = std::to_string(d_);
        profile_["scalar_mds"] = "jerasure";
        profile_["technique"] = "reed_sol_van";
    }

    bool initialize(std::string& error_msg) {
        std::ostringstream oss;
        int result = clay_->init(profile_, &oss);
        if (result != 0) {
            error_msg = oss.str();
            return false;
        }
        initialized_ = true;
        return true;
    }

    struct EncodeResult {
        bool success;
        std::map<int, bufferlist> chunks;
        size_t chunk_size;
        std::string error;
    };

    EncodeResult encode_data(const std::vector<uint8_t>& data) {
        EncodeResult result;
        
        if (!initialized_) {
            result.error = "Clay not initialized";
            return result;
        }

        bufferptr input_ptr = buffer::create_aligned(data.size(), ErasureCode::SIMD_ALIGN);
        input_ptr.copy_in(0, data.size(), reinterpret_cast<const char*>(data.data()));
        bufferlist input;
        input.push_back(std::move(input_ptr));

        std::set<int> want_to_encode;
        for (int i = 0; i < clay_->get_chunk_count(); ++i) {
            want_to_encode.insert(i);
        }

        int encode_result = clay_->encode(want_to_encode, input, &result.chunks);
        if (encode_result != 0) {
            result.error = "Encoding failed with code: " + std::to_string(encode_result);
            return result;
        }

        result.success = true;
        result.chunk_size = result.chunks.empty() ? 0 : result.chunks.begin()->second.length();
        return result;
    }

    struct DecodeResult {
        bool success;
        std::vector<uint8_t> data;
        std::string error;
    };

    DecodeResult decode_chunks(const std::map<int, bufferlist>& available_chunks, 
                               size_t original_size) {
        DecodeResult result;

        if (!initialized_) {
            result.error = "Clay not initialized";
            return result;
        }

        if (available_chunks.size() < static_cast<size_t>(k_)) {
            result.error = "Insufficient chunks for decoding";
            return result;
        }

        std::set<int> want_to_read;
        for (int i = 0; i < k_; ++i) {
            want_to_read.insert(i);
        }

        std::map<int, bufferlist> decoded_chunks;
        size_t chunk_size = available_chunks.begin()->second.length();
        int decode_result = clay_->decode(want_to_read, available_chunks, &decoded_chunks, chunk_size);
        
        if (decode_result != 0) {
            result.error = "Decoding failed with code: " + std::to_string(decode_result);
            return result;
        }

        bufferlist reconstructed;
        for (int i = 0; i < k_; ++i) {
            reconstructed.append(decoded_chunks[i]);
        }

        result.data.resize(original_size);
        buffer::list_iterator iter = reconstructed.begin();
        iter.copy(original_size, reinterpret_cast<char*>(result.data.data()));
        
        result.success = true;
        return result;
    }

    int get_total_chunks() const { return initialized_ ? clay_->get_chunk_count() : 0; }
    int get_data_chunks() const { return k_; }
    int get_coding_chunks() const { return m_; }
    int get_fault_tolerance() const { return m_; }
    size_t get_chunk_size(size_t data_size) const { 
        return initialized_ ? clay_->get_chunk_size(data_size) : 0; 
    }
};

void demonstrate_basic_usage() {
    std::cout << "=== Basic Usage Example ===" << std::endl;
    
    ClayWrapper clay(6, 3, 8);  // k = 6, m = 3, d = 8
    std::string error;
    
    if (!clay.initialize(error)) {
        std::cerr << "Failed to initialize: " << error << std::endl;
        return;
    }

    std::cout << "Initialized Clay with:" << std::endl;
    std::cout << "  Data chunks: " << clay.get_data_chunks() << std::endl;
    std::cout << "  Coding chunks: " << clay.get_coding_chunks() << std::endl;
    std::cout << "  Fault tolerance: " << clay.get_fault_tolerance() << " chunks" << std::endl;

    std::vector<uint8_t> original_data(2048);
    for (size_t i = 0; i < original_data.size(); ++i) {
        original_data[i] = static_cast<uint8_t>(i % 256);
    }

    auto encode_result = clay.encode_data(original_data);
    if (!encode_result.success) {
        std::cerr << "Encode failed: " << encode_result.error << std::endl;
        return;
    }

    std::cout << "Encoded " << original_data.size() << " bytes into " 
              << encode_result.chunks.size() << " chunks of " 
              << encode_result.chunk_size << " bytes each" << std::endl;

    auto available_chunks = encode_result.chunks;
    available_chunks.erase(1);  // lose chunk 1,
    available_chunks.erase(4);  // chunk 4 and
    available_chunks.erase(7);  // chunk 7
    
    std::cout << "Lost 3 chunks, " << available_chunks.size() << " remaining" << std::endl;

    auto decode_result = clay.decode_chunks(available_chunks, original_data.size());
    if (!decode_result.success) {
        std::cerr << "Decode failed: " << decode_result.error << std::endl;
        return;
    }

    bool matches = (original_data == decode_result.data);
    std::cout << "Data recovery: " << (matches ? "SUCCESS" : "FAILED") << std::endl;
    std::cout << std::endl;
}

void demonstrate_error_handling() {
    std::cout << "Error Handling" << std::endl;
    
    ClayWrapper clay;

    std::map<int, bufferlist> insufficient_chunks;
    bufferlist dummy_chunk;
    bufferptr dummy_ptr = buffer::create_aligned(100, ErasureCode::SIMD_ALIGN);
    dummy_chunk.push_back(std::move(dummy_ptr));
    
    insufficient_chunks[0] = dummy_chunk;
    insufficient_chunks[1] = dummy_chunk;
    
    std::string error;
    if (clay.initialize(error)) {
        auto decode_result = clay.decode_chunks(insufficient_chunks, 100);
        std::cout << "Expected error caught: " << decode_result.error << std::endl;
    }
    std::cout << std::endl;
}

void demonstrate_performance_characteristics() {
    std::cout << "Performance Characteristics" << std::endl;
    
    std::vector<size_t> data_sizes = {1024, 4096, 16384, 65536};
    ClayWrapper clay(4, 2, 5);
    
    std::string error;
    if (!clay.initialize(error)) {
        std::cerr << "Failed to initialize: " << error << std::endl;
        return;
    }

    for (size_t size : data_sizes) {
        std::vector<uint8_t> test_data(size, 42);
        
        auto start = std::chrono::high_resolution_clock::now();
        auto encode_result = clay.encode_data(test_data);
        auto encode_end = std::chrono::high_resolution_clock::now();
        
        if (!encode_result.success) continue;

        auto chunks_for_decode = encode_result.chunks;
        chunks_for_decode.erase(1);
        
        auto decode_start = std::chrono::high_resolution_clock::now();
        auto decode_result = clay.decode_chunks(chunks_for_decode, size);
        auto decode_end = std::chrono::high_resolution_clock::now();
        
        auto encode_time = std::chrono::duration_cast<std::chrono::microseconds>(encode_end - start).count();
        auto decode_time = std::chrono::duration_cast<std::chrono::microseconds>(decode_end - decode_start).count();
        
        double encode_throughput = (size * 1000000.0) / encode_time / (1024 * 1024);  
        double decode_throughput = (size * 1000000.0) / decode_time / (1024 * 1024);
        
        std::cout << "Size: " << size << " bytes" << std::endl;
        std::cout << "  Encode: " << encode_time << "µs (" << encode_throughput << " MB/s)" << std::endl;
        std::cout << "  Decode: " << decode_time << "µs (" << decode_throughput << " MB/s)" << std::endl;
        std::cout << "  Chunk size: " << clay.get_chunk_size(size) << " bytes" << std::endl;
        std::cout << std::endl;
    }
}

int main() {
    std::cout << "Clay Library Integration Demo" << std::endl;
    std::cout << std::endl;
    
    try {
        demonstrate_basic_usage();
        demonstrate_error_handling(); 
        demonstrate_performance_characteristics();
        
        std::cout << "Integration demo completed successfully!" << std::endl;
        std::cout << "This example shows how to:" << std::endl;
        std::cout << "• Wrap Clay in application-specific classes" << std::endl;
        std::cout << "• Handle errors gracefully" << std::endl;
        std::cout << "• Manage resources properly" << std::endl;
        std::cout << "• Measure performance characteristics" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}