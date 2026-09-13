#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace eve::cmd::sdk::detail {

class Sha256 {
public:
    void        update(const char* data, std::size_t size);
    std::string finish();

private:
    void transform(const uint8_t* block);

    std::array<uint32_t, 8> state_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<uint8_t, 64> buffer_{};
    uint64_t                totalBytes_ = 0;
    std::size_t             buffered_   = 0;
};

}  // namespace eve::cmd::sdk::detail
