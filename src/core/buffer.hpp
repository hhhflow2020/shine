#pragma once

#include "core/common.hpp"

#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

namespace shine {

// Reference-counted contiguous byte slab for zero-copy data frames.
class BufferSlab {
public:
    explicit BufferSlab(std::size_t capacity) : data_(capacity), size_(0) {}
    BufferSlab(const BufferSlab&)            = delete;
    BufferSlab& operator=(const BufferSlab&) = delete;

    u8*         data()       noexcept { return data_.data(); }
    const u8*   data() const noexcept { return data_.data(); }
    std::size_t size() const noexcept { return size_; }
    std::size_t capacity() const noexcept { return data_.size(); }

    void resize(std::size_t n) noexcept { size_ = n; }

    std::span<const u8> span() const noexcept {
        return {data_.data(), size_};
    }

private:
    std::vector<u8> data_;
    std::size_t     size_;
};

using BufferSlabPtr = std::shared_ptr<BufferSlab>;

// Simple growable ring-free buffer for parser input: a linear vector with
// consume/compact semantics. Good cache locality for typical RESP2 workloads.
class ReadBuffer {
public:
    explicit ReadBuffer(std::size_t initial = 16 * 1024) : buf_(initial) {}

    // Writable area for next async_read_some.
    u8*         writable_ptr()  noexcept { return buf_.data() + size_; }
    std::size_t writable_size() const noexcept { return buf_.size() - size_; }

    void committed(std::size_t n) noexcept { size_ += n; }

    // Readable view.
    const u8*   data() const noexcept { return buf_.data() + head_; }
    std::size_t size() const noexcept { return size_ - head_; }

    void consume(std::size_t n) noexcept {
        head_ += n;
        if (head_ >= size_) {
            head_ = 0;
            size_ = 0;
        } else if (head_ > buf_.size() / 2) {
            std::memmove(buf_.data(), buf_.data() + head_, size_ - head_);
            size_ -= head_;
            head_ = 0;
        }
    }

    void ensure_writable(std::size_t n) {
        if (writable_size() >= n) return;
        if (head_ > 0) consume(0); // compact
        if (writable_size() >= n) return;
        std::size_t need = size_ - head_ + n;
        std::size_t cap  = std::max(buf_.size() * 2, need);
        buf_.resize(cap);
    }

private:
    std::vector<u8> buf_;
    std::size_t     head_ = 0;
    std::size_t     size_ = 0;
};

} // namespace shine
