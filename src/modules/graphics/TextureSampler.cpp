#include "graphics/TextureSampler.h"

#include <algorithm>
#include <cmath>

namespace eve::graphics {
namespace {

bool equalsIgnoreCaseAscii(const char* value, const char* expected) {
    if (value == nullptr || expected == nullptr) return false;
    while (*value != '\0' && *expected != '\0') {
        const char a = (*value >= 'A' && *value <= 'Z') ? static_cast<char>(*value - 'A' + 'a') : *value;
        const char b = (*expected >= 'A' && *expected <= 'Z') ? static_cast<char>(*expected - 'A' + 'a') : *expected;
        if (a != b) return false;
        ++value;
        ++expected;
    }
    return *value == '\0' && *expected == '\0';
}

}  // namespace

FilterMode TextureSampler::parseFilter(const char* name) {
    if (equalsIgnoreCaseAscii(name, "nearest")) return FilterMode::Nearest;
    return FilterMode::Linear;
}

MipmapMode TextureSampler::parseMipmap(const char* name) {
    if (name == nullptr || name[0] == '\0' || equalsIgnoreCaseAscii(name, "none")) return MipmapMode::Disabled;
    if (equalsIgnoreCaseAscii(name, "nearest")) return MipmapMode::Nearest;
    return MipmapMode::Linear;
}

int mipmapCountForSize(int width, int height) {
    if (width <= 0 || height <= 0) return 1;
    const int m = std::max(width, height);
    return static_cast<int>(std::floor(std::log2(static_cast<double>(m)))) + 1;
}

}  // namespace eve::graphics
