#pragma once
#include "common/Export.h"

/**
 * @file UiMotionSinks.h
 * @brief UI-owned Motion push binders (optional animation bridge).
 *
 * These sinks live in `ui` so `animation` never `#include`s UI. The matching
 * `.cpp` is compiled only when the animation module is enabled
 * (`OPTIONAL_DEPS animation` + `EVENGINE_EXCLUDED_MODULE_FILES`).
 *
 * Keep each sink alive while the bound Motion is active; writes resolve the
 * host handle every sample and return `StaleHandle` / `NotFound` when the
 * target is gone.
 */

#include "animation/MotionTypes.h"
#include "ui/UIHost.h"

#include <string>

namespace eve::ui {

/**
 * @brief Pushes MotionVec2 samples into a host window position (Meta.posX/Y).
 * @ownership Does not own the host; resolves `host` on every write.
 */
class EVENGINE_API_WORLD UiHostPosSink final : public eve::animation::IMotionVec2Sink {
public:
    explicit UiHostPosSink(UIHostHandle host) : host_(host) {}

    /** @brief Write interpolated host position in pixels. */
    [[nodiscard]] eve::Result<void> write(eve::animation::MotionVec2 value) override;

private:
    UIHostHandle host_{};
};

/**
 * @brief Pushes MotionVec2 samples into a host explicit size (Meta.sizeX/Y).
 * @ownership Does not own the host; resolves `host` on every write.
 */
class EVENGINE_API_WORLD UiHostSizeSink final : public eve::animation::IMotionVec2Sink {
public:
    explicit UiHostSizeSink(UIHostHandle host) : host_(host) {}

    [[nodiscard]] eve::Result<void> write(eve::animation::MotionVec2 value) override;

private:
    UIHostHandle host_{};
};

/**
 * @brief Pushes float samples into a host overlay background alpha.
 * @ownership Does not own the host; resolves `host` on every write.
 */
class EVENGINE_API_WORLD UiHostOverlayAlphaSink final : public eve::animation::IMotionFloatSink {
public:
    explicit UiHostOverlayAlphaSink(UIHostHandle host) : host_(host) {}

    [[nodiscard]] eve::Result<void> write(float value) override;

private:
    UIHostHandle host_{};
};

/**
 * @brief Pushes float samples into a retained node's transient opacity.
 * @ownership Does not own the host/node; resolves both on every write.
 */
class EVENGINE_API_WORLD UiNodeOpacitySink final : public eve::animation::IMotionFloatSink {
public:
    UiNodeOpacitySink(UIHostHandle host, std::string nodeId)
        : host_(host), nodeId_(std::move(nodeId)) {}

    [[nodiscard]] eve::Result<void> write(float value) override;

private:
    UIHostHandle host_{};
    std::string  nodeId_;
};

/**
 * @brief Pushes MotionVec2 samples into a node's absolute placement offset.
 * @ownership Does not own the host/node; resolves both on every write.
 * @note Marks the node absolute so layout honors the offset.
 */
class EVENGINE_API_WORLD UiNodePosSink final : public eve::animation::IMotionVec2Sink {
public:
    UiNodePosSink(UIHostHandle host, std::string nodeId)
        : host_(host), nodeId_(std::move(nodeId)) {}

    [[nodiscard]] eve::Result<void> write(eve::animation::MotionVec2 value) override;

private:
    UIHostHandle host_{};
    std::string  nodeId_;
};

}  // namespace eve::ui
