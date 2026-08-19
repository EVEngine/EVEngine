#pragma once

#include <cstdint>
#include <string>

namespace eve::map {

struct MapObject {
    std::string name;
    std::string type;
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    uint32_t gid = 0;
};

}  // namespace eve::map
