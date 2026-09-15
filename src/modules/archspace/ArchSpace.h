#pragma once

#include "common/Module.h"

namespace eve::archspace {

/** @brief Runtime ArchSpace module entry used for profile wiring and script discovery. */
class ArchSpace final : public Module {
public:
    Module_REG(ArchSpace);
};

}  // namespace eve::archspace
