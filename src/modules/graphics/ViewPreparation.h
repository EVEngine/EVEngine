#pragma once
#include <cstdint>
#include <functional>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::graphics {
class Graphics;
/** @brief Prepare view-dependent resources before any forward target is opened.
 * @thread Graphics thread only. Arguments are borrowed for the synchronous call.
 * @reentrancy Must not register/remove preparers, recursively render a view, or leave a render pass open.
 * @details A failure cancels this view before its forward draw; callbacks own their resource transactions. */
using ViewPreparation = std::function<Result<void>(Graphics&, const glm::mat4&, const glm::vec3&)>;
/** @brief Register a preparer for main and offscreen views; empty callbacks fail.
 * @ownership Registry owns the callable until removal; captured objects must outlive registration.
 * @return Stable removal token or structured failure, without partial registration.
 * @thread Graphics thread, outside dispatch. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<uint64_t> addViewPreparation(ViewPreparation callback);
/** @brief Remove a registration; missing/zero tokens are harmless.
 * @thread Graphics thread outside dispatch. Does not invoke the callback. */
EVENGINE_API_BACKENDS void removeViewPreparation(uint64_t token) noexcept;
namespace detail {
/** @brief Dispatch preparers before opening the destination; no callbacks means success.
 * @thread Graphics thread, non-reentrant. No locks are held while invoking callbacks.
 * @lifetime Borrowed graphics and camera values are valid only through this call.
 * @return First failure; no remaining preparers or destination draw are executed.
 * @cost Sum of registered preparation costs; unchanged inputs may use provider caches. */
[[nodiscard]] EVENGINE_API_BACKENDS Result<void> prepareViewResources(Graphics&        graphics,
                                                                      const glm::mat4& viewProjection,
                                                                      const glm::vec3& eye);
}  // namespace detail
}  // namespace eve::graphics
