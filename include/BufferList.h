#ifndef BUFFER_LIST_H
#define BUFFER_LIST_H

#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <memory>


class BufferList {
public:
    BufferList() = default;

    // Create a BufferList with a given size, aligned to align
    explicit BufferList(size_t size, size_t align = 1) {
        if (size > 0) {
            size_t aligned_size = align_size(size, align);
            data_.resize(aligned_size);
        }
    }

    // Copy data from a string or raw buffer
    BufferList(const char* data, size_t len, size_t align = 1) {
        if (len > 0) {
            size_t aligned_size = align_size(len, align);
            data_.resize(aligned_size);
            std::memcpy(data_.data(), data, len);
        }
    }

    // Copy constructor
    BufferList(const BufferList& other) = default;

    // Move constructor
    BufferList(BufferList&& other) noexcept : data_(std::move(other.data_)) {}

    // Copy assignment
    BufferList& operator=(const BufferList& other) = default;

    // Move assignment
    BufferList& operator=(BufferList&& other) noexcept {
        data_ = std::move(other.data_);
        return *this;
    }

    // Access raw data
    char* c_str() { return data_.data(); }
    const char* c_str() const { return data_.data(); }

    // Get length of data
    size_t length() const { return data_.size(); }

    // Check if buffer is contiguous (always true for this implementation)
    bool is_contiguous() const { return true; }

    // Substring extraction
    void substr_of(const BufferList& src, size_t pos, size_t len) {
        if (pos + len > src.length()) {
            throw std::out_of_range("BufferList::substr_of: invalid range");
        }
        size_t aligned_size = align_size(len, 1);
        data_.resize(aligned_size);
        std::memcpy(data_.data(), src.c_str() + pos, len);
    }

    // Rebuild with aligned size and memory
    void rebuild_aligned(size_t align) {
        if (data_.empty()) return;
        size_t aligned_size = align_size(data_.size(), align);
        std::vector<char> new_data(aligned_size);
        std::memcpy(new_data.data(), data_.data(), data_.size());
        data_ = std::move(new_data);
    }

    void rebuild_aligned_size_and_memory(size_t size, size_t align) {
        size_t aligned_size = align_size(size, align);
        std::vector<char> new_data(aligned_size);
        size_t copy_size = std::min(data_.size(), size);
        if (copy_size > 0) {
            std::memcpy(new_data.data(), data_.data(), copy_size);
        }
        data_ = std::move(new_data);
    }

    // Append a buffer
    void push_back(std::vector<char>&& buf) {
        data_ = std::move(buf);
    }

    // Append another BufferList
    void claim_append(BufferList& other) {
        if (!other.data_.empty()) {
            size_t old_size = data_.size();
            data_.resize(old_size + other.length());
            std::memcpy(data_.data() + old_size, other.c_str(), other.length());
            other.data_.clear();
        }
    }

    // Zero the buffer or a portion of it
    void zero() {
        if (!data_.empty()) {
            std::memset(data_.data(), 0, data_.size());
        }
    }

    void zero(size_t offset, size_t len) {
        if (offset + len > data_.size()) {
            throw std::out_of_range("BufferList::zero: invalid range");
        }
        std::memset(data_.data() + offset, 0, len);
    }

    // Swap with another BufferList
    void swap(BufferList& other) noexcept {
        data_.swap(other.data_);
    }

    // Clear the buffer
    void clear() { data_.clear(); }

private:
    std::vector<char> data_;

    static size_t align_size(size_t size, size_t align) {
        if (align == 0) return size;
        return ((size + align - 1) / align) * align;
    }
};


#endif
