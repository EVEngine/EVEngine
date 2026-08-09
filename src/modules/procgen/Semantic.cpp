#include "procgen/Semantic.h"

#include <unordered_map>

namespace eve::procgen {
namespace {

const std::unordered_map<std::string, uint32_t> &nameToId() {
    static const std::unordered_map<std::string, uint32_t> m = {
        {"empty", Semantic::Empty},       {"wall", Semantic::Wall},
        {"floor", Semantic::Floor},       {"corridor", Semantic::Corridor},
        {"water", Semantic::Water},       {"sand", Semantic::Sand},
        {"grass", Semantic::Grass},       {"dirt", Semantic::Dirt},
        {"stone", Semantic::Stone},       {"snow", Semantic::Snow},
        {"door", Semantic::Door},
    };
    return m;
}

}  // namespace

const char *semanticName(uint32_t id) {
    switch (id) {
    case Semantic::Empty: return "empty";
    case Semantic::Wall: return "wall";
    case Semantic::Floor: return "floor";
    case Semantic::Corridor: return "corridor";
    case Semantic::Water: return "water";
    case Semantic::Sand: return "sand";
    case Semantic::Grass: return "grass";
    case Semantic::Dirt: return "dirt";
    case Semantic::Stone: return "stone";
    case Semantic::Snow: return "snow";
    case Semantic::Door: return "door";
    default: return "unknown";
    }
}

uint32_t semanticId(const std::string &name) {
    auto it = nameToId().find(name);
    return it == nameToId().end() ? Semantic::Empty : it->second;
}

}  // namespace eve::procgen
