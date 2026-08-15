#pragma once

#include "housegen/HouseComponentLibrary.h"

#include <string>

namespace eve::housegen {

class HouseLayout;

class HouseGenerator {
public:
    explicit HouseGenerator(const HouseComponentLibrary *library = nullptr) : library_(library) {}
    void setLibrary(const HouseComponentLibrary *library) { library_ = library; }
    const HouseComponentLibrary *library() const { return library_; }
    bool generate(const HouseRequest &request, HouseLayout &out, std::string *error = nullptr) const;

private:
    const HouseComponentLibrary *library_ = nullptr;
};

}  // namespace eve::housegen
