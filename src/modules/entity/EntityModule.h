#pragma once

#include "common/Module.h"

namespace eve {
namespace entity {

/**
 * Placeholder module marker. Script ECS is exposed globally via eve::exposeECS
 * (ModuleManager::expose): eve.Component / eve.Entity / eve.EntityContainer /
 * eve.System / eve.view. C++ gameplay entities continue to use ECS.hpp macros.
 */
class EntityModule : public Module {
public:
    std::string getName() const override { return "Entity"; }
};

}  // namespace entity
}  // namespace eve
