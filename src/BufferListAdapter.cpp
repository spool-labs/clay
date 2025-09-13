#include <clay/BufferList.h>
#include <clay/Buffer.h>
#include <cstring>
#include <stdexcept>
#include <iostream>

// BufferList adapter
BufferList::BufferList() : buffer_() {
}

BufferList::BufferList(size_t size, size_t alignment) : buffer_(size, alignment) {
}

BufferList::BufferList(const char* data, size_t size, size_t alignment) 
    : buffer_(data, size, alignment) {
}

BufferList::BufferList(const BufferList& other) : buffer_(other.buffer_) {
}

BufferList::BufferList(BufferList&& other) noexcept : buffer_(std::move(other.buffer_)) {
}

BufferList& BufferList::operator=(const BufferList& other) {
    if (this != &other) {
        buffer_ = other.buffer_;
    }
    return *this;
}

BufferList& BufferList::operator=(BufferList&& other) noexcept {
    if (this != &other) {
        buffer_ = std::move(other.buffer_);
    }
    return *this;
}

BufferList::~BufferList() = default;

char* BufferList::c_str() {
    char* result = buffer_.data();
    if (result && buffer_.size() > 0) {
        std::cout << "[DEBUG] BufferList::c_str() returning " << buffer_.size() 
                  << " bytes, first byte: 0x" << std::hex 
                  << static_cast<unsigned char>(result[0]) << std::dec << std::endl;
    } else {
        std::cout << "[DEBUG] BufferList::c_str() returning null or empty" << std::endl;
    }
    return result;
}

const char* BufferList::c_str() const {
    const char* result = buffer_.data();
    if (result && buffer_.size() > 0) {
        std::cout << "[DEBUG] BufferList::c_str() const returning " << buffer_.size() 
                  << " bytes, first byte: 0x" << std::hex 
                  << static_cast<unsigned char>(result[0]) << std::dec << std::endl;
    } else {
        std::cout << "[DEBUG] BufferList::c_str() const returning null or empty" << std::endl;
    }
    return result;
}

size_t BufferList::length() const {
    size_t len = buffer_.size();
    std::cout << "[DEBUG] BufferList::length() returning " << len << std::endl;
    return len;
}

bool BufferList::is_contiguous() const {
    return buffer_.is_contiguous();
}

void BufferList::clear() {
    std::cout << "[DEBUG] BufferList::clear() called" << std::endl;
    buffer_.clear();
}

void BufferList::append(const char* data, size_t size) {
    std::cout << "[DEBUG] BufferList::append() called with " << size << " bytes" << std::endl;
    if (data && size > 0) {
        std::cout << "[DEBUG] First byte being appended: 0x" << std::hex 
                  << static_cast<unsigned char>(data[0]) << std::dec << std::endl;
        buffer_.append(data, size);

        if (buffer_.size() >= size) {
            const char* stored = buffer_.data();
            if (stored) {
                std::cout << "[DEBUG] After append, first stored byte: 0x" << std::hex 
                          << static_cast<unsigned char>(stored[0]) << std::dec << std::endl;
            }
        }
    } else {
        std::cout << "[DEBUG] BufferList::append() called with null data or zero size" << std::endl;
    }
}

void BufferList::append(const BufferList& other) {
    if (!other.buffer_.empty()) {
        std::cout << "[DEBUG] BufferList::append(BufferList) called with " 
                  << other.buffer_.size() << " bytes" << std::endl;
        buffer_.append(other.buffer_.data(), other.buffer_.size());
    }
}

void BufferList::zero() {
    std::cout << "[DEBUG] BufferList::zero() called" << std::endl;
    buffer_.zero();
}

void BufferList::substr_of(const BufferList& other, size_t offset, size_t length) {
    std::cout << "[DEBUG] BufferList::substr_of() called, offset=" << offset 
              << ", length=" << length << std::endl;
    other.buffer_.substr_of(buffer_, offset, length);
}

void BufferList::claim_append(BufferList& other) {
    std::cout << "[DEBUG] BufferList::claim_append() called" << std::endl;
    buffer_.claim_append(other.buffer_);
}

void BufferList::rebuild_aligned_size_and_memory(size_t new_size, size_t alignment) {
    std::cout << "[DEBUG] BufferList::rebuild_aligned_size_and_memory() called, new_size=" 
              << new_size << ", alignment=" << alignment << std::endl;
    buffer_.rebuild_aligned_size_and_memory(new_size, alignment);
}

clay::Buffer& BufferList::get_buffer() {
    return buffer_;
}

const clay::Buffer& BufferList::get_buffer() const {
    return buffer_;
}