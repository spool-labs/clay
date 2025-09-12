#ifndef BUFFER_H
#define BUFFER_H

#include <cassert>
#include <cstring>
#include <list>
#include <memory>
#include <stdexcept>

namespace buffer {

inline namespace v1_0_0 {

// Forward declaration of raw buffer and list
class raw;
class list;

// Exception for allocation failures
struct bad_alloc : public std::bad_alloc {
    const char* what() const noexcept override { return "buffer::bad_alloc"; }
};

// Non-owning unique_ptr-like wrapper to signal ownership without automatic deletion
template <class T>
struct unique_leakable_ptr : public std::unique_ptr<T, void(*)(T*)> {
    using std::unique_ptr<T, void(*)(T*)>::unique_ptr;
    unique_leakable_ptr() : std::unique_ptr<T, void(*)(T*)>(nullptr, [](T*) {}) {}
    explicit unique_leakable_ptr(T* ptr)
        : std::unique_ptr<T, void(*)(T*)>(ptr, [](T*) {}) {}
};

// Raw buffer class to manage allocated memory
class raw {
public:
    raw(char* d, unsigned l, unsigned a) : data(d), len(l), align(a), nref(1) {}
    ~raw() { if (data) free(data); }

    char* get_data() const { return data; }
    unsigned get_len() const { return len; }
    unsigned get_align() const { return align; }
    void increment_ref() { ++nref; }
    bool decrement_ref() { return --nref == 0; }

private:
    char* data;
    unsigned len;
    unsigned align;
    unsigned nref; // Reference count
};

// Named constructor for raw buffers
inline unique_leakable_ptr<raw> create_aligned_raw(unsigned len, unsigned align) {
    void* ptr = nullptr;
    if (posix_memalign(&ptr, align, len) != 0) {
        throw bad_alloc();
    }
    memset(ptr, 0, len); // Initialize to zero
    return unique_leakable_ptr<raw>(new raw(static_cast<char*>(ptr), len, align));
}

// Buffer pointer class
class ptr {
protected:
    raw* _raw;
    unsigned _off, _len;

    void release() {
        if (_raw && _raw->decrement_ref()) {
            delete _raw;
        }
        _raw = nullptr;
        _off = _len = 0;
    }

public:
    ptr() : _raw(nullptr), _off(0), _len(0) {}
    explicit ptr(unique_leakable_ptr<raw> r)
        : _raw(r.release()), _off(0), _len(_raw ? _raw->get_len() : 0) {}
    ptr(const ptr& other)
        : _raw(other._raw), _off(other._off), _len(other._len) {
        if (_raw) _raw->increment_ref();
    }
    ptr(ptr&& other) noexcept
        : _raw(other._raw), _off(other._off), _len(other._len) {
        other._raw = nullptr;
        other._off = other._len = 0;
    }
    ptr(const ptr& other, unsigned o, unsigned l)
        : _raw(other._raw), _off(other._off + o), _len(l) {
        assert(_raw && (_off + l <= _raw->get_len()));
        _raw->increment_ref();
    }
    ~ptr() { release(); }

    ptr& operator=(const ptr& other) {
        if (this != &other) {
            release();
            _raw = other._raw;
            _off = other._off;
            _len = other._len;
            if (_raw) _raw->increment_ref();
        }
        return *this;
    }
    ptr& operator=(ptr&& other) noexcept {
        if (this != &other) {
            release();
            _raw = other._raw;
            _off = other._off;
            _len = other._len;
            other._raw = nullptr;
            other._off = other._len = 0;
        }
        return *this;
    }

    char* c_str() { return _raw ? _raw->get_data() + _off : nullptr; }
    const char* c_str() const { return _raw ? _raw->get_data() + _off : nullptr; }
    unsigned length() const { return _len; }
    unsigned offset() const { return _off; }
    unsigned raw_length() const { return _raw ? _raw->get_len() : 0; }

    void zero(bool = true) {
        if (_raw && _len) {
            memset(_raw->get_data() + _off, 0, _len);
        }
    }
    void zero(unsigned o, unsigned l) {
        assert(_raw && (o + l <= _len));
        memset(_raw->get_data() + _off + o, 0, l);
    }

    void copy_in(unsigned o, unsigned l, const char* src) {
        assert(_raw && (o + l <= _len));
        memcpy(_raw->get_data() + _off + o, src, l);
    }

    void copy_out(unsigned o, unsigned l, char* dest) const {
        assert(_raw && (o + l <= _len));
        memcpy(dest, _raw->get_data() + _off + o, l);
    }

    bool is_zero() const {
        if (!_raw || !_len) return true;
        const char* p = _raw->get_data() + _off;
        for (unsigned i = 0; i < _len; ++i) {
            if (p[i] != 0) return false;
        }
        return true;
    }
};

// Buffer list iterator
class list_iterator {
    const list* bl;
    std::list<ptr>::const_iterator p;
    unsigned off; // in bl
    unsigned p_off; // in *p

public:
    list_iterator() : bl(nullptr), off(0), p_off(0) {}
    list_iterator(const list* l, unsigned o);
    void copy(unsigned len, char* dest);
    bool end() const;
};

// Buffer list class
class list {
private:
    std::list<ptr> _buffers;
    unsigned _len;

public:
    list() : _len(0) {}
    list(const list& other) : _buffers(other._buffers), _len(other._len) {}
    list(list&& other) noexcept : _buffers(std::move(other._buffers)), _len(other._len) {
        other._len = 0;
    }
    ~list() = default;

    list& operator=(const list& other) {
        if (this != &other) {
            _buffers = other._buffers;
            _len = other._len;
        }
        return *this;
    }
    list& operator=(list&& other) noexcept {
        if (this != &other) {
            _buffers = std::move(other._buffers);
            _len = other._len;
            other._len = 0;
        }
        return *this;
    }

    void push_back(const ptr& bp) {
        if (bp.length() == 0) return;
        _buffers.push_back(bp);
        _len += bp.length();
    }
    void push_back(ptr&& bp) {
        if (bp.length() == 0) return;
        _buffers.push_back(std::move(bp));
        _len += _buffers.back().length();
    }

    void append(const ptr& bp) { push_back(bp); }
    void append(const list& other) {
        for (const auto& bp : other._buffers) {
            push_back(bp);
        }
    }

    void claim_append(list& other) {
        if (other._len == 0) return;
        _buffers.splice(_buffers.end(), other._buffers);
        _len += other._len;
        other._len = 0;
    }

    void swap(list& other) noexcept {
        _buffers.swap(other._buffers);
        std::swap(_len, other._len);
    }

    void clear() noexcept {
        _buffers.clear();
        _len = 0;
    }

    unsigned length() const { return _len; }

    bool is_contiguous() const { return _buffers.size() <= 1; }

    void rebuild_aligned(unsigned align) {
        if (_buffers.empty()) return;
        if (_buffers.size() == 1 && _buffers.front().is_zero()) {
            ptr new_ptr(create_aligned_raw(_len, align));
            _buffers.clear();
            _buffers.push_back(std::move(new_ptr));
            return;
        }
        ptr new_ptr(create_aligned_raw(_len, align));
        unsigned offset = 0;
        for (const auto& bp : _buffers) {
            new_ptr.copy_in(offset, bp.length(), bp.c_str());
            offset += bp.length();
        }
        _buffers.clear();
        _buffers.push_back(std::move(new_ptr));
    }

    bool rebuild_aligned_size_and_memory(unsigned align_size, unsigned align_memory, unsigned = 0) {
        if (_buffers.empty() || (_buffers.size() == 1 && _len % align_size == 0)) {
            return true;
        }
        ptr new_ptr(create_aligned_raw((_len + align_size - 1) / align_size * align_size, align_memory));
        unsigned offset = 0;
        for (const auto& bp : _buffers) {
            new_ptr.copy_in(offset, bp.length(), bp.c_str());
            offset += bp.length();
        }
        new_ptr.zero(offset, new_ptr.length() - offset);
        _buffers.clear();
        _buffers.push_back(std::move(new_ptr));
        return true;
    }

    void substr_of(const list& other, unsigned off, unsigned len) {
        clear();
        unsigned remaining = len;
        unsigned current_off = off;
        for (const auto& bp : other._buffers) {
            if (current_off >= bp.length()) {
                current_off -= bp.length();
                continue;
            }
            unsigned copy_len = std::min(bp.length() - current_off, remaining);
            ptr new_ptr(bp, current_off, copy_len);
            push_back(new_ptr);
            remaining -= copy_len;
            current_off = 0;
            if (remaining == 0) break;
        }
        assert(remaining == 0);
    }

    const std::list<ptr>& buffers() const { return _buffers; }

    char* c_str() {
        if (_buffers.size() != 1) {
            rebuild_aligned(alignof(void*));
        }
        return _buffers.empty() ? nullptr : _buffers.front().c_str();
    }

    bool is_zero() const {
        for (const auto& bp : _buffers) {
            if (!bp.is_zero()) return false;
        }
        return true;
    }

    list_iterator begin(unsigned offset = 0) const { return list_iterator(this, offset); }
};

// Named constructor
inline ptr create_aligned(unsigned len, unsigned align) {
    return ptr(create_aligned_raw(len, align));
}

// Iterator implementation
inline list_iterator::list_iterator(const list* l, unsigned o)
    : bl(l), p(l->buffers().begin()), off(o), p_off(0) {
    while (p != l->buffers().end() && o >= p->length()) {
        o -= p->length();
        ++p;
    }
    p_off = o;
}

inline void list_iterator::copy(unsigned len, char* dest) {
    while (len > 0 && p != bl->buffers().end()) {
        unsigned copy_len = std::min(len, p->length() - p_off);
        p->copy_out(p_off, copy_len, dest);
        dest += copy_len;
        len -= copy_len;
        off += copy_len;
        p_off += copy_len;
        if (p_off >= p->length()) {
            ++p;
            p_off = 0;
        }
    }
    assert(len == 0);
}

inline bool list_iterator::end() const {
    return off == bl->length();
}

} // inline namespace v1_0_0
} // namespace buffer

using bufferptr = buffer::ptr;
using bufferlist = buffer::list;

#endif // BUFFER_H
