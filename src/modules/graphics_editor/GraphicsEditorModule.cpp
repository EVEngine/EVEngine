#include "graphics_editor/GraphicsEditorModule.h"

#include "graphics_editor/ReflectionProbeVisualizer.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <functional>

namespace eve::graphics_editor {

Module_IMPL(GraphicsEditorModule, new GraphicsEditorModule());

std::unique_ptr<editor::ReflectionProbeVisualizer> GraphicsEditorModule::newReflectionProbeVisualizer(
    graphics::ReflectionProbeCapture* probe) const {
    return std::make_unique<editor::ReflectionProbeVisualizer>(probe);
}

void GraphicsEditorModule::expose(ssq::Table& table) {
    auto visualizer = table.addClass<editor::ReflectionProbeVisualizer>(
        "ReflectionProbeVisualizer",
        std::function<editor::ReflectionProbeVisualizer*()>(
            []() -> editor::ReflectionProbeVisualizer* { return nullptr; }),
        true);
    visualizer.addFunc("setExtents", &editor::ReflectionProbeVisualizer::setExtents);
    visualizer.addFunc("getLineCount", &editor::ReflectionProbeVisualizer::getLineCount);
    visualizer.addFunc("getLineStart", &editor::ReflectionProbeVisualizer::getLineStart);
    visualizer.addFunc("getLineEnd", &editor::ReflectionProbeVisualizer::getLineEnd);
    visualizer.addFunc("getColorR", &editor::ReflectionProbeVisualizer::getColorR);
    visualizer.addFunc("getColorG", &editor::ReflectionProbeVisualizer::getColorG);
    visualizer.addFunc("getColorB", &editor::ReflectionProbeVisualizer::getColorB);
    visualizer.addFunc("getCenterX", &editor::ReflectionProbeVisualizer::getCenterX);
    visualizer.addFunc("getCenterY", &editor::ReflectionProbeVisualizer::getCenterY);
    visualizer.addFunc("getCenterZ", &editor::ReflectionProbeVisualizer::getCenterZ);
    visualizer.addFunc("getStatusLabel", &editor::ReflectionProbeVisualizer::getStatusLabel);

    auto module = table.addClass(name, GraphicsEditorModule::create, false);
    expose(module);
}

void GraphicsEditorModule::expose(ssq::Class& cls) {
    cls.addFunc("createReflectionProbeVisualizer",
                [](GraphicsEditorModule* self, graphics::ReflectionProbeCapture* probe) {
                    return self->newReflectionProbeVisualizer(probe).release();
                });
}

}  // namespace eve::graphics_editor
