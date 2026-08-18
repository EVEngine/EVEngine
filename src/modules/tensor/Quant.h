#ifndef EVE_TENSOR_QUANT_H
#define EVE_TENSOR_QUANT_H

#include "tensor/Tensor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace eve::tensor {
namespace q {

/** True for the weight-quantization dtypes (stored packed, dequantized on use). */
inline bool isQuantDType(DType dt) {
    return dt == DType::Fp16 || dt == DType::Fp8E4M3 || dt == DType::Fp4E2M1 ||
           dt == DType::Int8 || dt == DType::Int4;
}

/** Bytes needed to store `count` elements of `dt` (int4/fp4 pack two per byte). */
inline int quantByteSize(DType dt, int count) {
    switch (dt) {
        case DType::Fp16: return count * 2;
        case DType::Fp8E4M3:
        case DType::Int8: return count;
        case DType::Fp4E2M1:
        case DType::Int4: return (count + 1) / 2;
        default: return count * 4;
    }
}

// ---------------------------------------------------------------- fp16
inline uint16_t f32ToF16(float x) {
    uint32_t b;
    std::memcpy(&b, &x, 4);
    const uint32_t sign = (b >> 16) & 0x8000u;
    const int32_t e = int32_t((b >> 23) & 0xFFu) - 127 + 15;
    uint32_t m = b & 0x7FFFFFu;
    if (e >= 0x1F) return uint16_t(sign | 0x7C00u);  // inf / nan -> inf
    if (e <= 0) {
        if (e < -10) return uint16_t(sign);
        m |= 0x800000u;
        const uint32_t shift = uint32_t(14 - e);
        uint32_t half = m >> shift;
        const uint32_t rem = m & ((1u << shift) - 1u);
        if (rem > (1u << (shift - 1)) || (rem == (1u << (shift - 1)) && (half & 1u))) ++half;
        return uint16_t(sign | half);
    }
    uint32_t half = (uint32_t(e) << 10) | (m >> 13);
    const uint32_t rem = m & 0x1FFFu;
    if (rem > 0x1000u || (rem == 0x1000u && (half & 1u))) ++half;
    return uint16_t(sign | half);
}

inline float f16ToF32(uint16_t h) {
    const uint32_t sign = uint32_t(h & 0x8000u) << 16;
    uint32_t e = (h >> 10) & 0x1Fu;
    uint32_t m = h & 0x3FFu;
    uint32_t bits = 0;
    if (e == 0) {
        if (m != 0) {
            e = 127 - 15 + 1;
            while (!(m & 0x400u)) {
                m <<= 1;
                --e;
            }
            m &= 0x3FFu;
            bits = sign | (e << 23) | (m << 13);
        }
    } else if (e == 31) {
        bits = sign | 0x7F800000u | (m << 13);
    } else {
        bits = sign | ((e + (127 - 15)) << 23) | (m << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// ---------------------------------------------------------------- fp8 e4m3
inline float fp8E4M3ToF32(uint8_t v) {
    const uint32_t sign = uint32_t(v & 0x80u) << 24;
    const uint32_t e = (v >> 3) & 0xFu;
    const uint32_t m = v & 0x7u;
    uint32_t bits;
    if (e == 0) {
        bits = m == 0 ? sign : (sign | (uint32_t(127 - 6) << 23) | (m << 20));
    } else {
        bits = sign | (uint32_t(e + 120) << 23) | (m << 20);  // 2^(e-7) -> fp32 exp e+120
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// ---------------------------------------------------------------- fp4 e2m1
inline float fp4E2M1ToF32(uint8_t nib) {
    const float sign = (nib & 0x8u) ? -1.f : 1.f;
    const int e = int((nib >> 1) & 0x3u);
    const int m = int(nib & 0x1u);
    const float v = e == 0 ? 0.5f * float(m) : std::ldexp(1.0f, e - 1) * (1.0f + 0.5f * float(m));
    return sign * v;
}

/**
 * Round-trip encode of an arbitrary float into a tiny e/m format (nearest,
 * via a cached value table + binary search — no per-element exponential math).
 */
inline uint32_t floatToEfm(float x, int expBits, int manBits, int bias) {
    struct EfmEntry {
        float value;
        uint32_t bits;
    };
    static std::vector<EfmEntry> *cache = nullptr;
    static int cacheExp = 0, cacheMan = 0, cacheBias = 0;
    if (!cache || cacheExp != expBits || cacheMan != manBits || cacheBias != bias) {
        delete cache;
        cache = new std::vector<EfmEntry>();
        const int expCount = 1 << expBits;
        const int manCount = 1 << manBits;
        for (uint32_t e = 0; e < uint32_t(expCount); ++e) {
            for (uint32_t m = 0; m < uint32_t(manCount); ++m) {
                float v = e == 0
                              ? std::ldexp(float(m), 1 - bias - manBits)
                              : std::ldexp(1.0f + float(m) / float(manCount), int(e) - bias);
                cache->push_back({v, (e << manBits) | m});
            }
        }
        std::sort(cache->begin(), cache->end(),
                  [](const EfmEntry &a, const EfmEntry &b) { return a.value < b.value; });
        cacheExp = expBits;
        cacheMan = manBits;
        cacheBias = bias;
    }
    if (std::isnan(x) || std::isinf(x)) {
        const uint32_t maxExp = (1u << expBits) - 2u;
        const uint32_t maxBits = (maxExp << manBits) | ((1u << manBits) - 1u);
        const uint32_t signBit = 1u << (expBits + manBits);
        return x < 0 ? (signBit | maxBits) : maxBits;
    }
    const float ax = std::fabs(x);
    size_t lo = 0, hi = cache->size();
    while (lo + 1 < hi) {
        const size_t mid = (lo + hi) / 2;
        if ((*cache)[mid].value <= ax) lo = mid;
        else hi = mid;
    }
    size_t best = lo;
    if (lo + 1 < cache->size() &&
        std::fabs((*cache)[lo + 1].value - ax) < std::fabs((*cache)[lo].value - ax))
        best = lo + 1;
    const uint32_t bits = (*cache)[best].bits;
    const uint32_t signBit = 1u << (expBits + manBits);
    return x < 0 ? (signBit | bits) : bits;
}

inline uint8_t f32ToFp8E4M3(float x) {
    return uint8_t(floatToEfm(x, 4, 3, 7));
}

inline uint8_t f32ToFp4E2M1(float x) {
    return uint8_t(floatToEfm(x, 2, 1, 1) & 0xFu);
}

/** Largest finite magnitude of an e/m format (used for block scaling). */
inline float efmMaxMagnitude(int expBits, int manBits, int bias) {
    const int maxExp = (1 << expBits) - 2;
    const int maxMan = (1 << manBits) - 1;
    return std::ldexp(1.0f + float(maxMan) / float(1 << manBits), maxExp - bias);
}

// ---------------------------------------------------------------- dequant
inline float dequantValue(DType dt, const uint8_t *bytes, const float *scales, int group,
                          int idx) {
    switch (dt) {
        case DType::Fp16: {
            uint16_t h;
            std::memcpy(&h, bytes + size_t(idx) * 2, 2);
            return f16ToF32(h);
        }
        case DType::Fp8E4M3:
            return fp8E4M3ToF32(bytes[static_cast<size_t>(idx)]) *
                   scales[static_cast<size_t>(idx) / size_t(group)];
        case DType::Int8: {
            const int8_t v = int8_t(bytes[static_cast<size_t>(idx)]);
            return float(v) * scales[static_cast<size_t>(idx) / size_t(group)];
        }
        case DType::Fp4E2M1: {
            const uint8_t byte = bytes[static_cast<size_t>(idx) / 2];
            const uint8_t nib = (idx & 1) ? uint8_t(byte >> 4) : uint8_t(byte & 0xFu);
            return fp4E2M1ToF32(nib) * scales[static_cast<size_t>(idx) / size_t(group)];
        }
        case DType::Int4: {
            const uint8_t byte = bytes[static_cast<size_t>(idx) / 2];
            int nib = (idx & 1) ? int(byte >> 4) : int(byte & 0xFu);
            if (nib >= 8) nib -= 16;
            return float(nib) * scales[static_cast<size_t>(idx) / size_t(group)];
        }
        default: return 0.f;
    }
}

inline void dequantizeAll(DType dt, const uint8_t *bytes, const float *scales, int group,
                          int count, float *out) {
    for (int i = 0; i < count; ++i) out[static_cast<size_t>(i)] = dequantValue(dt, bytes, scales, group, i);
}

// ---------------------------------------------------------------- quantize
struct QuantPayload {
    std::vector<uint8_t> bytes;
    std::vector<float> scales;
    int group = 0;
};

inline QuantPayload quantize(const float *src, int count, DType dt, int group) {
    QuantPayload p;
    p.group = group <= 0 ? count : group;
    p.bytes.assign(static_cast<size_t>(quantByteSize(dt, count)), 0);
    if (dt == DType::Int8 || dt == DType::Int4 || dt == DType::Fp8E4M3 ||
        dt == DType::Fp4E2M1) {
        // Per-group block scaling (MXFP-style) for the int and tiny-float
        // formats: value = dequant(norm) * scale. int formats normalize to
        // ±maxQ; fp8/fp4 normalize to their largest finite magnitude so the
        // whole exponent/mantissa range is used (not just [0,1]).
        const float maxQ = dt == DType::Int8   ? 127.f
                           : dt == DType::Int4 ? 7.f
                           : dt == DType::Fp8E4M3
                               ? efmMaxMagnitude(4, 3, 7)
                               : efmMaxMagnitude(2, 1, 1);  // Fp4E2M1
        const int groups = (count + p.group - 1) / p.group;
        p.scales.resize(static_cast<size_t>(groups));
        for (int g = 0; g < groups; ++g) {
            float maxAbs = 0.f;
            const int begin = g * p.group;
            const int end = std::min(count, begin + p.group);
            for (int i = begin; i < end; ++i) maxAbs = std::max(maxAbs, std::fabs(src[static_cast<size_t>(i)]));
            float scale = maxAbs / float(maxQ);
            if (scale == 0.f) scale = 1.f;
            p.scales[static_cast<size_t>(g)] = scale;
            for (int i = begin; i < end; ++i) {
                const float norm = src[static_cast<size_t>(i)] / scale;
                if (dt == DType::Fp8E4M3) {
                    p.bytes[static_cast<size_t>(i)] = f32ToFp8E4M3(norm);
                    continue;
                }
                if (dt == DType::Fp4E2M1) {
                    const uint8_t nib = f32ToFp4E2M1(norm);
                    uint8_t &byte = p.bytes[static_cast<size_t>(i) / 2];
                    if (i & 1) byte = uint8_t((byte & 0x0Fu) | uint8_t(nib << 4));
                    else byte = uint8_t((byte & 0xF0u) | nib);
                    continue;
                }
                int qv = int(std::floor(norm + 0.5f));
                const int qBound = int(maxQ);
                qv = std::max(-qBound - 1, std::min(qBound, qv));
                if (dt == DType::Int8) {
                    p.bytes[static_cast<size_t>(i)] = uint8_t(int8_t(qv));
                } else {
                    const int nib = qv & 0xFu;
                    uint8_t &byte = p.bytes[static_cast<size_t>(i) / 2];
                    if (i & 1) byte = uint8_t((byte & 0x0Fu) | uint8_t(nib << 4));
                    else byte = uint8_t((byte & 0xF0u) | uint8_t(nib));
                }
            }
        }
        return p;
    }
    for (int i = 0; i < count; ++i) {
        const float v = src[static_cast<size_t>(i)];
        switch (dt) {
            case DType::Fp16: {
                const uint16_t h = f32ToF16(v);
                std::memcpy(p.bytes.data() + size_t(i) * 2, &h, 2);
                break;
            }
            case DType::Fp8E4M3: p.bytes[static_cast<size_t>(i)] = f32ToFp8E4M3(v); break;
            case DType::Fp4E2M1: {
                const uint8_t nib = f32ToFp4E2M1(v);
                uint8_t &byte = p.bytes[static_cast<size_t>(i) / 2];
                if (i & 1) byte = uint8_t((byte & 0x0Fu) | uint8_t(nib << 4));
                else byte = uint8_t((byte & 0xF0u) | nib);
                break;
            }
            default: break;
        }
    }
    return p;
}

}  // namespace q
}  // namespace eve::tensor

#endif  // EVE_TENSOR_QUANT_H
