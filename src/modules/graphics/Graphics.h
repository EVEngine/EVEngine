#pragma once

#include "common/Module.h"

namespace eve {
namespace graphics {

class Graphics : public Module {
public:
    virtual ~Graphics() {}

    // Implements Module.
    virtual std::string getName() const { return name; }
    static const char*  name;
};

}  // namespace graphics
}  // namespace eve
