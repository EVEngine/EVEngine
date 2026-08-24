#pragma once

#include "attributes/AttributeSet.h"
#include "common/Module.h"

#include <string>

namespace eve::attributes {

/** @brief Generic named numeric attributes and modifier module. */
class Attributes : public Module {
public:
    Module_REG(Attributes);
    Attributes()           = default;
    ~Attributes() override = default;

    /** @brief Create a caller-owned set associated with an optional stable subject id. */
    AttributeSet* newSet(const std::string& subject = {});
};

}  // namespace eve::attributes
