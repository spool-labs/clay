#ifndef CLAY_BUFFER_H
#define CLAY_BUFFER_H

#include <cstddef>
#include <cstring>
#include <memory>

namespace clay {


class Buffer {
public:
    Buffer();
    explicit Buffer(size_t size, size_t alignment = 32);
    Buffer(const void* data, size_t size, size_t alignment = 32);
    Buffer(const Buffer& other);
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(const Buffer& other);
    Buffer& operator=(Buffer&& other) noexcept;
    ~Buffer();
    char* data() { return data_; }
    const char* data() const { return data_; }
    const char* c_str() const { return data_; }
    size_t size() const { return size_; }
    size_t length() const { return size_; }
    bool empty() const { return size_ == 0; }
    size_t capacity() const { return capacity_; }
    bool is_contiguous() const { return true; }
    void resize(size_t new_size, bool preserve_data = true);
    void clear();
    void reset();
    void assign(const void* data, size_t size);
    void append(const void* data, size_t size);
    void zero();
    void substr_of(Buffer& buffer, size_t offset, size_t length) const;
    void claim_append(Buffer& other);
    void rebuild_aligned_size_and_memory(size_t new_size, size_t alignment);

    static constexpr size_t default_alignment() { return 32; }

private:
    char* data_;           ///< pointer to buffer data
    size_t size_;          ///< current size of valid data
    size_t capacity_;      ///< total allocated capacity
    size_t alignment_;     ///< memory alignment

    static void* aligned_alloc(size_t size, size_t alignment);

    static void aligned_free(void* ptr);

    void ensure_capacity(size_t min_capacity);
};

} 

#endif 