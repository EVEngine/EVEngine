#pragma once

#include "editor/EditorProtocol.h"
#include "editor/EditorTarget.h"

#include <string>
#include <vector>

namespace eve::editor {

/** @brief Stable discovery metadata for an editable target. */
struct TargetDescriptor {
    TargetId                  id;
    std::string               type;
    Revision                  revision = 0;
    bool                      readOnly = false;
    std::vector<CapabilityId> capabilities;
};

/** @brief Explicit stable-ID capability discovery layered over the V1 target. */
class IEditableTargetV2 : public virtual IEditableTarget {
public:
    ~IEditableTargetV2() override = default;
    /** @brief Return immutable target discovery metadata. */
    virtual TargetDescriptor describe() const = 0;
    /** @brief Return a capability interface pointer or nullptr for an unsupported id. */
    virtual void* queryCapability(const CapabilityId& capability) = 0;
};

}  // namespace eve::editor
