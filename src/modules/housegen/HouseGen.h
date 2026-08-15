#pragma once

#include "common/Module.h"
#include "housegen/HouseComponentLibrary.h"
#include "housegen/HouseGenerator.h"
#include "housegen/HouseLayout.h"

namespace eve::housegen {

class HouseGen : public Module {
public:
    Module_REG(HouseGen);
    bool loadComponentsFromJson(const std::string &json);
    bool loadComponentsFromFile(const std::string &filename);
    void clearComponents();
    int getComponentCount() const;
    HouseRequest *newRequest();
    HouseLayout *newLayout();
    bool generate(HouseRequest *request, HouseLayout *layout);
    std::string lastError() const;
    HouseComponentLibrary &library() { return library_; }
    const HouseComponentLibrary &library() const { return library_; }

private:
    HouseComponentLibrary library_;
    std::string lastError_;
};

}  // namespace eve::housegen
