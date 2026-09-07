#pragma once

#include "common/Module.h"

#include <memory>
#include <string>

namespace eve::editor {
class EditorSession;
}
namespace eve::map {
class TileLayer;
}
namespace eve::map_editing {
class TileLayerTarget;
}

namespace eve::tilelayer_target {

/** @brief Script-facing composition module for live tile-layer editing targets. */
class TileLayerTargetModule final : public Module {
public:
    Module_REG(TileLayerTargetModule);

    /**
     * @brief Create an adapter over a borrowed live tile layer.
     * @param id Stable target identity.
     * @param layer Borrowed layer that must outlive the returned adapter.
     * @return Independently owned target adapter.
     * @thread Owner-thread only.
     */
    [[nodiscard]] std::unique_ptr<map_editing::TileLayerTarget> newTarget(std::string id, map::TileLayer* layer) const;
    /**
     * @brief Bind a target to an editor session.
     * @param session Borrowed session that remains owned by the caller.
     * @param target Borrowed target that must outlive the binding.
     * @thread Owner-thread only.
     */
    void bind(editor::EditorSession* session, map_editing::TileLayerTarget* target) const;
};

}  // namespace eve::tilelayer_target
