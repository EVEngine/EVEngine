#include "editor/ReflectionProbeVisualizer.h"

#include "graphics/ReflectionProbeCapture.h"

#include <algorithm>
#include <array>
#include <sstream>

namespace eve::editor {
namespace {

constexpr std::array<std::array<int, 2>, 12> kEdges{{
    {{0, 1}}, {{1, 3}}, {{3, 2}}, {{2, 0}}, {{4, 5}}, {{5, 7}},
    {{7, 6}}, {{6, 4}}, {{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}},
}};

}  // namespace

ReflectionProbeVisualizer::ReflectionProbeVisualizer(graphics::ReflectionProbeCapture *probe)
    : probe_(probe) {}

void ReflectionProbeVisualizer::setExtents(float x, float y, float z) {
    if (!probe_) return;
    probe_->configureInfluence(x, y, z, probe_->getInfluenceIntensity(),
                               probe_->getInfluenceBlendDistance(),
                               probe_->getInfluencePriority());
}

float ReflectionProbeVisualizer::coordinate(int corner, int component) const {
    if (!probe_ || corner < 0 || corner >= 8 || component < 0 || component >= 3) return 0.f;
    const float center[3] = {probe_->getCenterX(), probe_->getCenterY(), probe_->getCenterZ()};
    const float extent[3] = {probe_->getInfluenceExtentX(), probe_->getInfluenceExtentY(),
                             probe_->getInfluenceExtentZ()};
    return center[component] +
           ((corner & (1 << component)) ? extent[component] : -extent[component]);
}

float ReflectionProbeVisualizer::getLineStart(int line, int component) const {
    if (line < 0 || line >= int(kEdges.size())) return 0.f;
    return coordinate(kEdges[static_cast<size_t>(line)][0], component);
}

float ReflectionProbeVisualizer::getLineEnd(int line, int component) const {
    if (line < 0 || line >= int(kEdges.size())) return 0.f;
    return coordinate(kEdges[static_cast<size_t>(line)][1], component);
}

float ReflectionProbeVisualizer::getColorR() const {
    if (!probe_) return 1.f;
    if (probe_->isCapturePending()) return 1.f;
    if (probe_->getPublishedRevision() == probe_->getRevision()) return 0.2f;
    return 0.15f;
}

float ReflectionProbeVisualizer::getColorG() const {
    if (!probe_) return 0.15f;
    if (probe_->isCapturePending()) return 0.55f;
    if (probe_->getPublishedRevision() == probe_->getRevision()) return 1.f;
    return 0.8f;
}

float ReflectionProbeVisualizer::getColorB() const {
    if (!probe_) return 0.15f;
    if (probe_->isCapturePending()) return 0.1f;
    if (probe_->getPublishedRevision() == probe_->getRevision()) return 0.35f;
    return 1.f;
}

float ReflectionProbeVisualizer::getCenterX() const { return probe_ ? probe_->getCenterX() : 0.f; }
float ReflectionProbeVisualizer::getCenterY() const { return probe_ ? probe_->getCenterY() : 0.f; }
float ReflectionProbeVisualizer::getCenterZ() const { return probe_ ? probe_->getCenterZ() : 0.f; }

std::string ReflectionProbeVisualizer::getStatusLabel() const {
    if (!probe_) return "Reflection probe: detached";
    std::ostringstream label;
    label << "Reflection probe " << probe_->getRevision() << '/' << probe_->getStagedRevision()
          << '/' << probe_->getPublishedRevision();
    if (probe_->isCapturePending())
        label << " capture " << (6 - probe_->getPendingFaceCount()) << "/6";
    else if (probe_->getPublishedRevision() != probe_->getRevision())
        label << " filtering";
    else
        label << " ready";
    if (probe_->isRecaptureQueued()) label << " +queued";
    return label.str();
}

}  // namespace eve::editor
