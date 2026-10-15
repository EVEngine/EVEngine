#pragma once

#include <complex>
#include <numbers>
#include <vector>

namespace eve::fluids::ocean_detail {
using Complex               = std::complex<double>;
inline constexpr double tau = 2.0 * std::numbers::pi;
// Unnormalized inverse transform: coefficients represent physical amplitudes,
// so no N-dependent scale enters either wave height or the authoring parameters.
inline void inverse1D(std::vector<Complex>& a) {
    const auto n = a.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        auto bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const auto step = std::polar(1.0, tau / static_cast<double>(length));
        for (std::size_t start = 0; start < n; start += length) {
            Complex w{1.0, 0.0};
            for (std::size_t j = 0; j < length / 2; ++j) {
                const auto even = a[start + j], odd = a[start + j + length / 2] * w;
                a[start + j]              = even + odd;
                a[start + j + length / 2] = even - odd;
                w *= step;
            }
        }
    }
}
inline void inverse2D(std::vector<Complex>& a, unsigned n) {
    std::vector<Complex> line(n);
    for (unsigned z = 0; z < n; ++z) {
        for (unsigned x = 0; x < n; ++x) line[x] = a[z * n + x];
        inverse1D(line);
        for (unsigned x = 0; x < n; ++x) a[z * n + x] = line[x];
    }
    for (unsigned x = 0; x < n; ++x) {
        for (unsigned z = 0; z < n; ++z) line[z] = a[z * n + x];
        inverse1D(line);
        for (unsigned z = 0; z < n; ++z) a[z * n + x] = line[z];
    }
}

// Normalized forward transform paired with the physical-amplitude inverse.
inline void forward2D(std::vector<Complex>& a, unsigned n) {
    for (auto& value : a) value = std::conj(value);
    inverse2D(a, n);
    const double scale = 1.0 / double(n * n);
    for (auto& value : a) value = std::conj(value) * scale;
}
}  // namespace eve::fluids::ocean_detail
