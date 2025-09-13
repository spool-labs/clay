#ifndef CLAY_BUFFERLIST_H
#define CLAY_BUFFERLIST_H

#include <clay/Buffer.h>
#include <cstddef>

class BufferList {
public:

    BufferList();
    explicit BufferList(size_t size, size_t alignment = 32);
    BufferList(const char* data, size_t size, size_t alignment = 32);
    BufferList(const BufferList& other);
    BufferList(BufferList&& other) noexcept;
    BufferList& operator=(const BufferList& other);
    BufferList& operator=(BufferList&& other) noexcept;
    ~BufferList();
    char* c_str();
    const char* c_str() const;
    size_t length() const;
    bool is_contiguous() const;
    void clear();
    void append(const char* data, size_t size);
    void append(const BufferList& other);
    void zero();
    void substr_of(const BufferList& other, size_t offset, size_t length);
    void claim_append(BufferList& other);
    void rebuild_aligned_size_and_memory(size_t new_size, size_t alignment);

    clay::Buffer& get_buffer();
    const clay::Buffer& get_buffer() const;

private:
    clay::Buffer buffer_;  ///< underlying buffer object
};

#endif 