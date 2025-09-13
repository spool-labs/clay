#include <clay/Buffer.h>
#include <algorithm>
#include <stdexcept>
#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#else
#include <cstdlib>
#endif

namespace clay {

void* Buffer::aligned_alloc(size_t size, size_t alignment) {
    if (size == 0) return nullptr;

    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        alignment = default_alignment();
    }

#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) {
        return nullptr;
    }
    return ptr;
#endif
}

void Buffer::aligned_free(void* ptr) {
    if (ptr) {
#ifdef _WIN32
        _aligned_free(ptr);
#else
        free(ptr);
#endif
    }
}


Buffer::Buffer() 
    : data_(nullptr), size_(0), capacity_(0), alignment_(default_alignment()) {
}

Buffer::Buffer(size_t size, size_t alignment)
    : data_(nullptr), size_(0), capacity_(0), alignment_(alignment) {
    if (size > 0) {
        resize(size, false);
    }
}

Buffer::Buffer(const void* data, size_t size, size_t alignment)
    : data_(nullptr), size_(0), capacity_(0), alignment_(alignment) {
    if (data && size > 0) {
        assign(data, size);
    }
}

Buffer::Buffer(const Buffer& other)
    : data_(nullptr), size_(0), capacity_(0), alignment_(other.alignment_) {
    if (other.size_ > 0) {
        assign(other.data_, other.size_);
    }
}

Buffer::Buffer(Buffer&& other) noexcept
    : data_(other.data_), size_(other.size_), capacity_(other.capacity_), alignment_(other.alignment_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.capacity_ = 0;
}

Buffer& Buffer::operator=(const Buffer& other) {
    if (this != &other) {
        alignment_ = other.alignment_;
        if (other.size_ > 0) {
            assign(other.data_, other.size_);
        } else {
            clear();
        }
    }
    return *this;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        aligned_free(data_);
        data_ = other.data_;
        size_ = other.size_;
        capacity_ = other.capacity_;
        alignment_ = other.alignment_;
        
        other.data_ = nullptr;
        other.size_ = 0;
        other.capacity_ = 0;
    }
    return *this;
}

Buffer::~Buffer() {
    aligned_free(data_);
}


void Buffer::resize(size_t new_size, bool preserve_data) {
    if (new_size == size_) {
        return;
    }

    if (new_size == 0) {
        clear();
        return;
    }

    if (new_size <= capacity_) {
        if (!preserve_data || new_size > size_) {
            if (new_size > size_) {
                std::memset(data_ + size_, 0, new_size - size_);
            }
        }
        size_ = new_size;
        return;
    }

    size_t new_capacity = std::max(new_size, capacity_ * 2);
    void* new_data = aligned_alloc(new_capacity, alignment_);
    if (!new_data) {
        throw std::bad_alloc();
    }

    if (preserve_data && data_ && size_ > 0) {
        std::memcpy(new_data, data_, std::min(size_, new_size));
    }

    if (new_size > size_) {
        std::memset(static_cast<char*>(new_data) + size_, 0, new_size - size_);
    }

    aligned_free(data_);
    data_ = static_cast<char*>(new_data);
    size_ = new_size;
    capacity_ = new_capacity;
}

void Buffer::clear() {
    size_ = 0;
}

void Buffer::reset() {
    aligned_free(data_);
    data_ = nullptr;
    size_ = 0;
    capacity_ = 0;
}

void Buffer::assign(const void* data, size_t size) {
    if (size == 0) {
        clear();
        return;
    }

    resize(size, false);
    if (data) {
        std::memcpy(data_, data, size);
    }
}

void Buffer::append(const void* data, size_t append_size) {
    if (append_size == 0) {
        return;
    }

    size_t old_size = size_;
    resize(size_ + append_size, true);
    if (data) {
        std::memcpy(data_ + old_size, data, append_size);
    }
}

void Buffer::zero() {
    if (data_ && size_ > 0) {
        std::memset(data_, 0, size_);
    }
}

void Buffer::ensure_capacity(size_t min_capacity) {
    if (capacity_ < min_capacity) {
        size_t new_capacity = std::max(min_capacity, capacity_ * 2);
        void* new_data = aligned_alloc(new_capacity, alignment_);
        if (!new_data) {
            throw std::bad_alloc();
        }

        if (data_ && size_ > 0) {
            std::memcpy(new_data, data_, size_);
        }

        aligned_free(data_);
        data_ = static_cast<char*>(new_data);
        capacity_ = new_capacity;
    }
}

void Buffer::substr_of(Buffer& buffer, size_t offset, size_t length) const {
    if (offset >= size_) {
        buffer.clear();
        return;
    }

    length = std::min(length, size_ - offset);
    buffer.assign(data_ + offset, length);
}

void Buffer::claim_append(Buffer& other) {
    if (other.empty()) {
        return;
    }

    if (empty()) {
        *this = std::move(other);
    } else {
        append(other.data_, other.size_);
        other.clear();
    }
}

void Buffer::rebuild_aligned_size_and_memory(size_t new_size, size_t alignment) {
    if (alignment != alignment_ || new_size != size_) {
        Buffer temp;
        if (size_ > 0 && new_size > 0) {
            temp.assign(data_, std::min(size_, new_size));
        }
        reset();
        alignment_ = alignment;
        
        if (new_size > 0) {
            resize(new_size, false);
            if (!temp.empty()) {
                std::memcpy(data_, temp.data_, std::min(new_size, temp.size_));
            }
        }
    }
}

} 