#pragma once

#include "procgen/editing/SplinePathDocument.h"

#include <map>
#include <string>
#include <vector>

namespace eve {
class ISceneQuery;
}

namespace eve::procgen_editing {

/** @brief Value snapshot resolved from a bound transform source. */
struct SplineBindingPose {
    double x = 0.0, y = 0.0, z = 0.0;
};

/** @brief Consumer-owned transform lookup contract used by spline binding. */
class ISplineBindingTransformSource {
public:
    virtual ~ISplineBindingTransformSource() = default;
    /** @brief Resolve one stable source identity without returning borrowed storage. */
    [[nodiscard]] virtual EditorResult<SplineBindingPose> resolve(const std::string& host,
                                                                  const std::string& object) const = 0;
};

/** @brief Optional-scene adapter for the UI-neutral spline binding contract. */
class SceneQuerySplineBindingSource final : public ISplineBindingTransformSource {
public:
    /** @brief Construct from a borrowed capability; null represents a trimmed scene provider. */
    explicit SceneQuerySplineBindingSource(const ISceneQuery* query) : query_(query) {}
    [[nodiscard]] EditorResult<SplineBindingPose> resolve(const std::string& host,
                                                          const std::string& object) const override;

private:
    const ISceneQuery* query_ = nullptr;
};

/** @brief Persistent link from one spline control point to one scene transform identity. */
struct SplinePointBinding {
    StableId    point;
    std::string host;
    std::string object;
};

/**
 * @brief Owning, pointer-free spline-to-transform binder with atomic document refresh.
 *
 * The binder owns stable link identities but neither the spline document nor transform source.
 * Callers pass both to each operation, so destroying either first cannot leave a retained pointer.
 * Missing sources produce StaleHandle without changing the document or discarding the link, allowing
 * the same binding snapshot to rebuild after scene reload.
 */
class SplinePathBinder {
public:
    /** @brief Construct a binder pinned to one spline document target identity. */
    explicit SplinePathBinder(std::string documentTarget);
    /** @brief Validate and atomically install or replace one point binding. */
    [[nodiscard]] EditorResult<void> bindResult(const SplinePathDocument& document, const StableId& point,
                                                std::string host, std::string object,
                                                const ISplineBindingTransformSource& source);
    /** @brief Remove one binding without touching its spline point. */
    [[nodiscard]] EditorResult<void> unbindResult(const StableId& point);
    /** @brief Resolve every link, then commit all point positions as one isolated document update. */
    [[nodiscard]] EditorResult<void> refreshResult(SplinePathDocument&                  document,
                                                   const ISplineBindingTransformSource& source) const;
    /** @brief Return deterministic bindings ordered by stable point id. */
    [[nodiscard]] std::vector<SplinePointBinding> bindings() const;
    /** @brief Capture schema `eve.procgen.splineBinder` version one. */
    [[nodiscard]] EditorValue snapshotValue() const;
    /** @brief Atomically restore links for this binder's pinned target. */
    [[nodiscard]] EditorResult<void> loadSnapshot(const EditorValue& snapshot);

private:
    std::string                            documentTarget_;
    std::map<StableId, SplinePointBinding> bindings_;
};

}  // namespace eve::procgen_editing
