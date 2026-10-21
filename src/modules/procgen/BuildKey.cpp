#include "procgen/GeneratedArtifact.h"
#include "procgen/road/RoadAlgorithmRevision.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace eve::procgen {
namespace {
std::uint64_t fnv1a(std::string_view text) noexcept {
    std::uint64_t value = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        value ^= byte;
        value *= 1099511628211ull;
    }
    return value;
}

bool hasControl(std::string_view text) noexcept {
    for (const unsigned char byte : text) {
        if (std::iscntrl(byte) != 0) return true;
    }
    return false;
}

}  // namespace
std::optional<BuildKey> BuildKey::fromCanonical(std::string_view canonical) {
    if (canonical.empty() || hasControl(canonical)) return std::nullopt;
    return BuildKey(std::string(canonical));
}

std::optional<BuildKey> BuildKey::forRecipe(std::string_view recipeId, const Params &params) {
    if (recipeId.empty() || hasControl(recipeId)) return std::nullopt;
    if (recipeId == "mesh.roadNetwork") {
        std::ostringstream value;
        value << "procgen/v2/" << recipeId << '/' << road::detail::algorithmRevision << '/' << std::hex << std::setw(16)
              << std::setfill('0')
              << fnv1a(std::string(recipeId) + '\0' + std::string(road::detail::algorithmRevision) + '\0' +
                       params.canonicalString());
        return BuildKey(value.str());
    }
    std::ostringstream value;
    value << "procgen/v1/" << recipeId << '/' << std::hex << std::setw(16) << std::setfill('0')
          << fnv1a(std::string(recipeId) + '\0' + params.canonicalString());
    return BuildKey(value.str());
}

}  // namespace eve::procgen
