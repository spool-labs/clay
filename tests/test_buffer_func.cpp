#include <clay/Buffer.h>
#include <clay/BufferList.h>
#include <iostream>
#include <iomanip>
#include <cstring>

void print_hex(const char* label, const char* data, size_t size) {
    std::cout << label << ": ";
    for (size_t i = 0; i < size && i < 16; i++) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<unsigned char>(data[i]) << " ";
    }
    std::cout << std::dec << std::endl;
}

int main() {
    std::cout << "Testing Buffer and BufferList functionality" << std::endl;

    std::cout << "\nBasic Buffer" << std::endl;
    const char test_data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    clay::Buffer buffer(test_data, 5);
    
    std::cout << "Buffer size: " << buffer.size() << std::endl;
    if (buffer.data()) {
        print_hex("Buffer data", buffer.data(), buffer.size());
    } else {
        std::cout << "ERROR: Buffer data is null!" << std::endl;
    }

    std::cout << "\nBufferList" << std::endl;
    BufferList bl;
    bl.append(test_data, 5);
    
    std::cout << "BufferList length: " << bl.length() << std::endl;
    if (bl.c_str()) {
        print_hex("BufferList data", bl.c_str(), bl.length());
    } else {
        std::cout << "ERROR: BufferList data is null!" << std::endl;
    }

    std::cout << "\nData Integrity" << std::endl;
    bool integrity_ok = true;
    if (bl.length() == 5 && bl.c_str()) {
        for (size_t i = 0; i < 5; i++) {
            if (static_cast<unsigned char>(bl.c_str()[i]) != test_data[i]) {
                std::cout << "ERROR: Data mismatch at position " << i 
                          << " - expected 0x" << std::hex << static_cast<unsigned char>(test_data[i])
                          << " got 0x" << static_cast<unsigned char>(bl.c_str()[i]) << std::dec << std::endl;
                integrity_ok = false;
            }
        }
        if (integrity_ok) {
            std::cout << "SUCCESS: Data integrity preserved" << std::endl;
        }
    } else {
        std::cout << "ERROR: BufferList length or data pointer invalid" << std::endl;
        integrity_ok = false;
    }

    std::cout << "\nZero Detection" << std::endl;
    bool has_zeros = false;
    if (bl.c_str() && bl.length() > 0) {
        for (size_t i = 0; i < bl.length(); i++) {
            if (bl.c_str()[i] == 0) {
                has_zeros = true;
                break;
            }
        }
        std::cout << "Contains zeros: " << (has_zeros ? "YES" : "NO") << std::endl;
    }

    std::cout << "\nPattern Test, for Clay" << std::endl;
    const size_t pattern_size = 64;
    char pattern_data[pattern_size];
    for (size_t i = 0; i < pattern_size; i++) {
        pattern_data[i] = static_cast<char>(i + 1); 
    }
    
    BufferList pattern_bl;
    pattern_bl.append(pattern_data, pattern_size);
    
    std::cout << "Pattern BufferList length: " << pattern_bl.length() << std::endl;
    if (pattern_bl.c_str() && pattern_bl.length() >= 4) {
        print_hex("First 4 bytes", pattern_bl.c_str(), 4);
        print_hex("Last 4 bytes", pattern_bl.c_str() + pattern_bl.length() - 4, 4);
    }

    bool pattern_has_zeros = false;
    if (pattern_bl.c_str()) {
        for (size_t i = 0; i < pattern_bl.length(); i++) {
            if (pattern_bl.c_str()[i] == 0) {
                pattern_has_zeros = true;
                std::cout << "Found zero at position " << i << std::endl;
            }
        }
        if (!pattern_has_zeros) {
            std::cout << "Pattern contains no zeros - good for testing!" << std::endl;
        }
    }
    
    std::cout << "\n" << (integrity_ok && !has_zeros ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    
    return integrity_ok && !has_zeros ? 0 : 1;
}