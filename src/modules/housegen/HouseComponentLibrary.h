#pragma once

#include "housegen/HouseGenTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::housegen {

class HouseComponentLibrary {
public:
    bool loadFromJson(const std::string &json, std::string *error = nullptr);
    bool loadFromFile(const std::string &filename, std::string *error = nullptr);
    bool registerComponent(const HouseComponent &component, std::string *error = nullptr);
    void clear();
    const HouseComponent *find(const std::string &id) const;
    std::vector<std::string> ids() const;
    std::vector<const HouseComponent *> byCategory(const std::string &category,
                                                    const std::string &style = {}) const;
    int count() const;

private:
    std::unordered_map<std::string, HouseComponent> components_;
};

}  // namespace eve::housegen
