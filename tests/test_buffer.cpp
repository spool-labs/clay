#include <clay/Buffer.h>
#include <iostream>
#include <cassert>
#include <cstring>

using namespace clay;

void test_basic_operations() {
    std::cout << "Testing basic Buffer operations... ";

    Buffer buf1;
    assert(buf1.empty());
    assert(buf1.size() == 0);
    assert(buf1.data() == nullptr);

    Buffer buf2(1024);
    assert(!buf2.empty());
    assert(buf2.size() == 1024);
    assert(buf2.data() != nullptr);
    assert(buf2.is_contiguous());
    
    std::cout << "PASS\n";
}

void test_data_operations() {
    std::cout << "Testing Buffer data operations... ";
    
    const char* test_data = "Hello, Clay Buffer!";
    size_t test_size = strlen(test_data);

    Buffer buf(test_data, test_size);
    assert(buf.size() == test_size);
    assert(memcmp(buf.data(), test_data, test_size) == 0);

    Buffer buf2;
    buf2.assign(test_data, test_size);
    assert(buf2.size() == test_size);
    assert(memcmp(buf2.data(), test_data, test_size) == 0);

    buf2.append(" More data", 10);
    assert(buf2.size() == test_size + 10);
    
    std::cout << "PASS\n";
}

void test_copy_move() {
    std::cout << "Testing Buffer copy/move semantics... ";
    
    const char* test_data = "Test data for copy/move";
    Buffer original(test_data, strlen(test_data));

    Buffer copied(original);
    assert(copied.size() == original.size());
    assert(memcmp(copied.data(), original.data(), original.size()) == 0);
    assert(copied.data() != original.data()); // different memory

    size_t original_size = original.size();
    char* original_ptr = original.data();
    Buffer moved(std::move(original));
    assert(moved.size() == original_size);
    assert(moved.data() == original_ptr); // same memory
    assert(original.data() == nullptr); // original is moved from
    assert(original.size() == 0);
    
    std::cout << "PASS\n";
}

void test_substr_operations() {
    std::cout << "Testing Buffer substring operations... ";
    
    const char* test_data = "0123456789ABCDEF";
    Buffer original(test_data, 16);

    Buffer sub;
    original.substr_of(sub, 5, 5); 
    assert(sub.size() == 5);
    assert(memcmp(sub.data(), "56789", 5) == 0);

    Buffer buf1("Hello", 5);
    Buffer buf2(" World", 6);
    buf1.claim_append(buf2);
    assert(buf1.size() == 11);
    assert(memcmp(buf1.data(), "Hello World", 11) == 0);
    assert(buf2.empty()); // buf2 would nromally be empty after claim
    
    std::cout << "PASS\n";
}

void test_memory_alignment() {
    std::cout << "Testing Buffer memory alignment... ";
    
    Buffer buf(1024, 64); 

    uintptr_t addr = reinterpret_cast<uintptr_t>(buf.data());
    assert(addr % 64 == 0);

    buf.rebuild_aligned_size_and_memory(1024, 32);
    addr = reinterpret_cast<uintptr_t>(buf.data());
    assert(addr % 32 == 0);
    assert(buf.size() == 1024);
    
    std::cout << "PASS\n";
}

void test_resize_operations() {
    std::cout << "Testing Buffer resize operations... ";
    
    Buffer buf(100);
    buf.zero(); 

    buf.resize(200);
    assert(buf.size() == 200);

    buf.resize(50);
    assert(buf.size() == 50);

    buf.clear();
    assert(buf.size() == 0);
    assert(buf.capacity() > 0); 

    buf.reset();
    assert(buf.size() == 0);
    assert(buf.capacity() == 0);
    assert(buf.data() == nullptr);
    
    std::cout << "PASS\n";
}

int main() {
    std::cout << "Running Buffer class tests...\n";
    
    try {
        test_basic_operations();
        test_data_operations();
        test_copy_move();
        test_substr_operations();
        test_memory_alignment();
        test_resize_operations();
        
        std::cout << "\nAll Buffer tests passed!\n";
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << std::endl;
        return 1;
    }
}