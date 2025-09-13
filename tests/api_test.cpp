#include "ErasureCodeClay.h"
#include "BufferList.h"
#include <iostream>
#include <sstream>
#include <cassert>
#include <vector>
#include <random>

class ClayAPITester {
private:
    int tests_run_;
    int tests_passed_;
    
    void assert_test(bool condition, const std::string& test_name) {
        tests_run_++;
        if (condition) {
            tests_passed_++;
            std::cout << "[PASS] " << test_name << std::endl;
        } else {
            std::cout << "[FAIL] " << test_name << std::endl;
        }
    }

public:
    ClayAPITester() : tests_run_(0), tests_passed_(0) {}
    
    void test_initialization() {
        std::cout << "Testing Clay Initialization..." << std::endl;
        
        ErasureCodeClay clay(".");
        ErasureCodeProfile profile;
        profile["k"] = "4";
        profile["m"] = "2";
        profile["d"] = "5";
        profile["scalar_mds"] = "jerasure";
        profile["technique"] = "reed_sol_van";

        std::ostringstream oss;
        int result = clay.init(profile, &oss);
        
        assert_test(result == 0, "Clay initialization succeeds");
        assert_test(clay.k == 4, "k parameter set correctly");
        assert_test(clay.m == 2, "m parameter set correctly");  
        assert_test(clay.d == 5, "d parameter set correctly");
        assert_test(clay.get_chunk_count() == 6, "Total chunk count correct");
        assert_test(clay.get_data_chunk_count() == 4, "Data chunk count correct");
    }
    
    void test_invalid_parameters() {
        std::cout << "Testing Invalid Parameters..." << std::endl;

        {
            ErasureCodeClay clay(".");
            ErasureCodeProfile profile;
            profile["k"] = "1";  // invalid: k must be >= 2
            profile["m"] = "2";
            profile["d"] = "3";
            profile["scalar_mds"] = "jerasure";
            profile["technique"] = "reed_sol_van";

            std::ostringstream oss;
            int result = clay.init(profile, &oss);
            assert_test(result != 0, "Reject k=1 (too small)");
        }

        {
            ErasureCodeClay clay(".");
            ErasureCodeProfile profile;
            profile["k"] = "4";
            profile["m"] = "0";  // invalid: m must be >= 1
            profile["d"] = "4";
            profile["scalar_mds"] = "jerasure";
            profile["technique"] = "reed_sol_van";

            std::ostringstream oss;
            int result = clay.init(profile, &oss);
            assert_test(result != 0, "Reject m=0 (too small)");
        }

        {
            ErasureCodeClay clay(".");
            ErasureCodeProfile profile;
            profile["k"] = "4";
            profile["m"] = "2";
            profile["d"] = "7";  // invalid: d > k+m-1
            profile["scalar_mds"] = "jerasure";
            profile["technique"] = "reed_sol_van";

            std::ostringstream oss;
            int result = clay.init(profile, &oss);
            assert_test(result != 0, "Reject d > k+m-1");
        }
    }
    
    void test_encode_decode_basic() {
        std::cout << "Testing Basic Encode/Decode..." << std::endl;
        
        ErasureCodeClay clay(".");
        ErasureCodeProfile profile;
        profile["k"] = "4";
        profile["m"] = "2"; 
        profile["d"] = "5";
        profile["scalar_mds"] = "jerasure";
        profile["technique"] = "reed_sol_van";

        std::ostringstream oss;
        int result = clay.init(profile, &oss);
        assert_test(result == 0, "Clay initialized for encode/decode test");

        size_t data_size = 1024;
        std::vector<uint8_t> original_data(data_size);
        for (size_t i = 0; i < data_size; ++i) {
            original_data[i] = static_cast<uint8_t>(i % 256);
        }

        bufferptr input_ptr = buffer::create_aligned(data_size, ErasureCode::SIMD_ALIGN);
        input_ptr.copy_in(0, data_size, reinterpret_cast<const char*>(original_data.data()));
        bufferlist input;
        input.push_back(std::move(input_ptr));

        std::set<int> want_to_encode;
        for (int i = 0; i < clay.get_chunk_count(); ++i) {
            want_to_encode.insert(i);
        }
        
        std::map<int, bufferlist> encoded_chunks;
        result = clay.encode(want_to_encode, input, &encoded_chunks);
        assert_test(result == 0, "Encoding succeeds");
        assert_test(encoded_chunks.size() == 6, "Correct number of chunks created");

        size_t expected_chunk_size = clay.get_chunk_size(data_size);
        for (const auto& [index, chunk] : encoded_chunks) {
            assert_test(chunk.length() == expected_chunk_size, 
                       "Chunk " + std::to_string(index) + " has correct size");
        }

        std::set<int> want_to_read;
        for (int i = 0; i < clay.k; ++i) {
            want_to_read.insert(i);
        }
        
        std::map<int, bufferlist> decoded_chunks;
        result = clay.decode(want_to_read, encoded_chunks, &decoded_chunks, expected_chunk_size);
        assert_test(result == 0, "Decoding without loss succeeds");

        bufferlist reconstructed;
        for (int i = 0; i < clay.k; ++i) {
            reconstructed.append(decoded_chunks[i]);
        }
        
        std::vector<uint8_t> recovered_data(data_size);
        buffer::list_iterator iter = reconstructed.begin();
        iter.copy(data_size, reinterpret_cast<char*>(recovered_data.data()));
        
        assert_test(original_data == recovered_data, "Data integrity preserved");
    }
    
    void test_fault_tolerance() {
        std::cout << "Testing Fault Tolerance..." << std::endl;
        
        ErasureCodeClay clay(".");
        ErasureCodeProfile profile;
        profile["k"] = "4";
        profile["m"] = "2";
        profile["d"] = "5";
        profile["scalar_mds"] = "jerasure";
        profile["technique"] = "reed_sol_van";

        std::ostringstream oss;
        clay.init(profile, &oss);

        size_t data_size = 2048;
        std::vector<uint8_t> original_data(data_size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (size_t i = 0; i < data_size; ++i) {
            original_data[i] = static_cast<uint8_t>(dis(gen));
        }
        
        bufferptr input_ptr = buffer::create_aligned(data_size, ErasureCode::SIMD_ALIGN);
        input_ptr.copy_in(0, data_size, reinterpret_cast<const char*>(original_data.data()));
        bufferlist input;
        input.push_back(std::move(input_ptr));
        
        std::set<int> want_to_encode;
        for (int i = 0; i < clay.get_chunk_count(); ++i) {
            want_to_encode.insert(i);
        }
        
        std::map<int, bufferlist> encoded_chunks;
        clay.encode(want_to_encode, input, &encoded_chunks);

        {
            auto available = encoded_chunks;
            available.erase(1);
            
            std::set<int> want_to_read;
            for (int i = 0; i < clay.k; ++i) {
                want_to_read.insert(i);
            }
            
            std::map<int, bufferlist> decoded;
            size_t chunk_size = encoded_chunks.begin()->second.length();
            std::cout << "DEBUG - 1 chunk loss:" << std::endl;
            std::cout << "Total chunks: " << clay.get_chunk_count() << std::endl;
            std::cout << "Available chunks after loss: " << available.size() << std::endl;
            std::cout << "Need for recovery: " << clay.k << " chunks minimum" << std::endl; 
            std::cout << "Lost 3, have " << (6-3) << " = " << available.size() << std::endl;
            int result = clay.decode(want_to_read, available, &decoded, chunk_size);
            assert_test(result == 0, "Recovery from 1 chunk loss succeeds");
        }

        {
            auto available = encoded_chunks;
            available.erase(1); 
            available.erase(4); 
            
            std::set<int> want_to_read;
            for (int i = 0; i < clay.k; ++i) {
                want_to_read.insert(i);
            }
            
            std::map<int, bufferlist> decoded;
            size_t chunk_size = encoded_chunks.begin()->second.length();
            std::cout << "DEBUG - 2 chunk loss:" << std::endl;
            std::cout << "Total chunks: " << clay.get_chunk_count() << std::endl;
            std::cout << "Available chunks after loss: " << available.size() << std::endl;
            std::cout << "Need for recovery: " << clay.k << " chunks minimum" << std::endl;
            int result = clay.decode(want_to_read, available, &decoded, chunk_size);
            assert_test(result == 0, "Recovery from 2 chunk loss succeeds");
        }
        
        {
            auto available = encoded_chunks;
            available.erase(1); 
            available.erase(4); 
            available.erase(5); 
            
            std::set<int> want_to_read;
            for (int i = 0; i < clay.k; ++i) {
                want_to_read.insert(i);
            }
            
            std::map<int, bufferlist> decoded;
            size_t chunk_size = encoded_chunks.begin()->second.length();
            std::cout << "DEBUG - 3 chunk loss:" << std::endl;
            std::cout << "Clay configuration: k=" << clay.k << ", m=" << clay.m << std::endl;
            std::cout << "Total chunks created: " << clay.get_chunk_count() << std::endl;
            std::cout << "Theoretical fault tolerance: " << clay.m << " chunks" << std::endl;
            std::cout << "Available chunks after loss: " << available.size() << std::endl;
            std::cout << "Need for recovery: " << clay.k << " chunks minimum" << std::endl;
            std::cout << "Lost 3, have " << available.size() << " chunks" << std::endl;
            std::cout << "This should fail because " << available.size() << " < " << clay.k << std::endl;
            int result = clay.decode(want_to_read, available, &decoded, chunk_size);
            assert_test(result != 0, "Recovery from 3 chunk loss fails");
        }
    }
    
    void run_all_tests() {
        std::cout << "Clay API Test Suite" << std::endl;
        std::cout << std::endl;
        
        test_initialization();
        std::cout << std::endl;
        
        test_invalid_parameters();
        std::cout << std::endl;
        
        test_encode_decode_basic();
        std::cout << std::endl;
        
        test_fault_tolerance();
        std::cout << std::endl;
        
        print_summary();
    }
    
    void print_summary() {
        std::cout << "Test Summary" << std::endl;
        std::cout << "Tests run: " << tests_run_ << std::endl;
        std::cout << "Tests passed: " << tests_passed_ << std::endl;
        std::cout << "Tests failed: " << (tests_run_ - tests_passed_) << std::endl;
        
        if (tests_passed_ == tests_run_) {
            std::cout << std::endl;
            std::cout << "ALL TESTS PASSED! Clay is working correctly." << std::endl;
        } else {
            std::cout << std::endl;
            std::cout << "Some tests failed. Please check the Clay implementation." << std::endl;
        }
    }
    
    bool all_tests_passed() const {
        return tests_passed_ == tests_run_;
    }
};

int main() {
    ClayAPITester tester;
    tester.run_all_tests();
    return tester.all_tests_passed() ? 0 : 1;
}