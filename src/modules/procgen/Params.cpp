#include "procgen/Params.h"

#include <algorithm>
#include <cstdlib>

namespace eve::procgen {

void Params::setSeed(uint32_t seed) { seed_ = seed; }
uint32_t Params::getSeed() const { return seed_; }

void Params::setSize(int width, int height) {
    width_  = width > 0 ? width : 1;
    height_ = height > 0 ? height : 1;
}
int Params::getWidth() const { return width_; }
int Params::getHeight() const { return height_; }

void Params::setInt(const std::string &key, int value) {
    if (key == "seed") {
        seed_ = static_cast<uint32_t>(std::max(0, value));
        return;
    }
    if (key == "width") {
        width_ = value > 0 ? value : 1;
        return;
    }
    if (key == "height") {
        height_ = value > 0 ? value : 1;
        return;
    }
    values_[key] = std::to_string(value);
}
void Params::setFloat(const std::string &key, float value) { values_[key] = std::to_string(value); }
void Params::setString(const std::string &key, const std::string &value) { values_[key] = value; }

bool Params::has(const std::string &key) const {
    if (key == "seed" || key == "width" || key == "height") return true;
    return values_.find(key) != values_.end();
}

int Params::getInt(const std::string &key, int defaultValue) const {
    if (key == "seed") return static_cast<int>(seed_);
    if (key == "width") return width_;
    if (key == "height") return height_;
    auto it = values_.find(key);
    if (it == values_.end()) return defaultValue;
    char *end = nullptr;
    long  v   = std::strtol(it->second.c_str(), &end, 10);
    if (end == it->second.c_str()) return defaultValue;
    return int(v);
}

float Params::getFloat(const std::string &key, float defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end()) return defaultValue;
    char *end = nullptr;
    float v   = std::strtof(it->second.c_str(), &end);
    if (end == it->second.c_str()) return defaultValue;
    return v;
}

std::string Params::getString(const std::string &key, const std::string &defaultValue) const {
    auto it = values_.find(key);
    return it == values_.end() ? defaultValue : it->second;
}

}  // namespace eve::procgen
