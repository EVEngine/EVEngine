#include "common/Utf8Validation.h"

namespace eve {

bool isValidUtf8(std::string_view text, Utf8NullPolicy nullPolicy) noexcept {
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<std::uint8_t>(text[index]);
        if (lead < 0x80) {
            if (lead == 0 && nullPolicy == Utf8NullPolicy::Reject) return false;
            ++index;
            continue;
        }
        std::size_t continuation = 0;
        std::uint32_t codepoint = 0;
        if ((lead & 0xe0u) == 0xc0u) {
            continuation = 1;
            codepoint = lead & 0x1fu;
        } else if ((lead & 0xf0u) == 0xe0u) {
            continuation = 2;
            codepoint = lead & 0x0fu;
        } else if ((lead & 0xf8u) == 0xf0u) {
            continuation = 3;
            codepoint = lead & 0x07u;
        } else {
            return false;
        }
        if (continuation >= text.size() - index) return false;
        for (std::size_t part = 1; part <= continuation; ++part) {
            const auto byte = static_cast<std::uint8_t>(text[index + part]);
            if ((byte & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6u) | (byte & 0x3fu);
        }
        if ((continuation == 1 && codepoint < 0x80) ||
            (continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
        index += continuation + 1;
    }
    return true;
}

}  // namespace eve
