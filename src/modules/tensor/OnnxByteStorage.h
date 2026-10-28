#pragma once
#include <span>
#include <stdexcept>
#include "tensor/OnnxModel.h"
#include "tensor/OnnxStorage.h"
namespace eve::tensor::onnx_detail {
// Internal copy-on-write host view. Const reads materialize only at CPU boundaries.
// GPU aliases share immutable storage and never force a readback.
/** @brief ByteStorage public API. */
class ByteStorage {
    mutable OnnxBuffer                    buffer_;
    std::shared_ptr<std::vector<uint8_t>> writable_;
    const std::vector<uint8_t>&           get() const {
        if (!buffer_.host) {
            if (!buffer_.device) throw std::runtime_error("Missing ONNX storage");
            auto r = buffer_.device->readback();
            if (!r.ok()) throw std::runtime_error(r.error()->message());
            if (r.value().size() != buffer_.size) throw std::runtime_error("GPU byte count mismatch");
            buffer_.host = std::make_shared<const std::vector<uint8_t>>(std::move(r.value()));
        }
        return *buffer_.host;
    }
    std::vector<uint8_t>& write() {
        // CPU mutation creates a new authoritative storage identity.
        if (writable_ && buffer_.host.use_count() == 2) return *writable_;
        auto v    = std::make_shared<std::vector<uint8_t>>(get());
        buffer_   = {v, {}, v->size()};
        writable_ = v;
        return *v;
    }

public:
    /** @brief Constructs a ByteStorage. */
    ByteStorage() : ByteStorage(std::vector<uint8_t>{}) {}
    /** @brief Constructs a ByteStorage. */
    ByteStorage(std::vector<uint8_t> v) : buffer_{std::make_shared<const std::vector<uint8_t>>(std::move(v)), {}, 0} {
        buffer_.size = buffer_.host->size();
    }
    /** @brief Constructs a ByteStorage. */
    ByteStorage(OnnxBuffer b) : buffer_(std::move(b)) {}
    /** @brief Retain across runs. */
    void              retainAcrossRuns() { buffer_.persistent = true; }
    /** @brief Buffer. */
    const OnnxBuffer& buffer() const { return buffer_; }
    /** @brief Returns the size of size. */
    size_t            size() const { return buffer_.size; }
    /** @brief Empty. */
    bool              empty() const { return !size(); }
    /** @brief Borrow CPU data until this storage is mutated or destroyed; device-thread only for lazy data.
     * @lifetime Borrowed until mutation or destruction; use the provider thread for GPU readback.
     * @throws std::runtime_error On readback failure. Internal executor converts this to Result. */
    const uint8_t* data() const { return get().data(); }
    /** @brief Borrow mutable CPU data until mutation/destruction; detaches graph aliases before writing.
     * @lifetime Borrowed until mutation or destruction; use the provider thread for GPU readback.
     * @throws std::runtime_error On readback failure. Internal executor converts this to Result. */
    uint8_t* data() { return write().data(); }
    /** @brief Operator []. */
    uint8_t  operator[](size_t i) const { return get()[i]; }
    /** @brief Operator []. */
    uint8_t& operator[](size_t i) { return write()[i]; }
    /** @brief Begins begin. */
    auto     begin() { return write().begin(); }
    /** @brief Ends end. */
    auto     end() { return write().end(); }
    /** @brief Begins begin. */
    auto     begin() const { return get().begin(); }
    /** @brief Ends end. */
    auto     end() const { return get().end(); }
    /** @brief Resize. */
    void     resize(size_t n) {
        auto& v = write();
        v.resize(n);
        buffer_.size = n;
    }
    /** @brief Copy borrowed iterators into owning CPU storage.
     * @lifetime Input iterators are borrowed only for this call; all prior data pointers are invalidated. */
    template <class I>
    /** @brief Assign. */
    void assign(I a, I b) {
        *this = ByteStorage(std::vector<uint8_t>(a, b));
    }
    operator std::span<const uint8_t>() const { return get(); }
    operator std::vector<uint8_t>() const { return get(); }
};
/** @brief RuntimeTensor public API. */
struct RuntimeTensor {
    OnnxElement          element = OnnxElement::Float32;
    std::vector<int64_t> shape;
    ByteStorage          bytes;
};
}  // namespace eve::tensor::onnx_detail
