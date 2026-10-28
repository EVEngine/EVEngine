#pragma once

#include "common/Module.h"

#include <memory>

namespace eve::graphics {
class ReflectionProbeCapture;
}
namespace eve::editor {
class ReflectionProbeVisualizer;
}

namespace eve::graphics_editor {

/** @brief Script composition module for graphics-domain editor helpers. */
class GraphicsEditorModule final : public Module {
public:
    Module_REG(GraphicsEditorModule);

    /**
     * @brief Create a renderer-independent reflection-probe visualizer.
     * @param probe Borrowed probe that must outlive the returned visualizer.
     * @return Independently owned visualizer.
     */
    [[nodiscard]] std::unique_ptr<editor::ReflectionProbeVisualizer> newReflectionProbeVisualizer(
        graphics::ReflectionProbeCapture* probe) const;
};

}  // namespace eve::graphics_editor
