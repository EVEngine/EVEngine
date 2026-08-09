#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace eve::procgen {

/**
 * Generation parameters. Algorithm-specific keys live in `values` as strings
 * (no overloads; typed setters/getters for script convenience).
 */
class Params {
public:
    void     setSeed(uint32_t seed);
    uint32_t getSeed() const;

    void setSize(int width, int height);
    int  getWidth() const;
    int  getHeight() const;

    void        setInt(const std::string &key, int value);
    void        setFloat(const std::string &key, float value);
    void        setString(const std::string &key, const std::string &value);
    bool        has(const std::string &key) const;
    int         getInt(const std::string &key, int defaultValue) const;
    float       getFloat(const std::string &key, float defaultValue) const;
    std::string getString(const std::string &key, const std::string &defaultValue) const;

private:
    uint32_t                                 seed_   = 1;
    int                                      width_  = 32;
    int                                      height_ = 32;
    std::unordered_map<std::string, std::string> values_;
};

}  // namespace eve::procgen
