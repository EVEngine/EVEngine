#pragma once

#include "vkbuilder.hpp"
#include "vkbuilder/framegraph.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace eve::thread {
class JobSystem;
}

namespace eve::graphics::vulkan {

/**
 * @brief Build a vkb::PassRecordExecutor backed by the engine JobSystem.
 *
 * The returned executor records every pass in a layer concurrently (one
 * frame-scoped job per pass), joins the layer, then continues with the next
 * layer. Exposed separately so the executor contract can be unit-tested
 * without a Vulkan device.
 *
 * @param jobs Engine JobSystem; must have an active beginFrame()/endFrame()
 *             bracket. May be null, in which case recording is serial.
 */
vkb::PassRecordExecutor jobSystemPassExecutor(eve::thread::JobSystem *jobs);

/**
 * @brief Record a vkb::FrameGraph with the engine JobSystem as the parallel
 * recording executor.
 *
 * vkb::FrameGraph groups independent passes into layers; passes inside one
 * layer share no dependency edge, so their command buffers can be recorded
 * concurrently. This executor forks one frame-scoped job per pass in the
 * layer, joins them, then moves to the next layer (layers stay sequential —
 * that is the framegraph's threading contract).
 *
 * The caller must keep a JobSystem frame bracket open (beginFrame() before,
 * endFrame() after) so the fork/join groups come from the per-frame arena and
 * recording allocates no job control blocks per frame.
 *
 * @param graph  Compiled FrameGraph whose passes should be recorded.
 * @param jobs   Engine JobSystem; must have an active beginFrame()/endFrame()
 *               bracket. May be null, in which case recording is serial.
 */
void recordFrameGraphWithJobSystem(vkb::FrameGraph &graph, eve::thread::JobSystem *jobs);

}  // namespace eve::graphics::vulkan
