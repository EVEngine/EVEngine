#pragma once
#include "common/Export.h"

#include "common/Module.h"

#include <memory>

namespace eve::animation {

/**
 * @brief Manager-owned MeshRet neural retarget satellite (animation + tensor).
 * @thread Owner/composition thread only; revokes capability before destruction.
 */
class EVENGINE_API_ORCHESTRATION AnimationTensor : public Module {
public:
    Module_REG(AnimationTensor);
    AnimationTensor();
    ~AnimationTensor() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace eve::animation
