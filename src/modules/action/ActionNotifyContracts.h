#pragma once

/**
 * @file ActionNotifyContracts.h
 * @brief Catalogue of L1 typed notify/block payload contracts owned by action.
 *
 * ## Ownership boundary
 *
 * These headers define **schema-only** payload types (`*Binding` / decode helpers)
 * and narrow sink interfaces (`IAction*Sink`). They must not create Scene, Audio,
 * Particles, or Combat mutable state. Domain mutation belongs to:
 *
 * - optional `IActionNotifyProvider` installers in presentation/combat satellites
 * - sink implementations registered through capabilities (`IActionDamageSink`, …)
 * - thin built-in handlers in action that only validate payloads and forward to sinks
 *
 * ## Include map
 *
 * | Contract header | Typical notify types |
 * | --- | --- |
 * | ActionSpatialBlock.h | shared spatial fields for audio/vfx/prefab |
 * | ActionDamageBlock.h | `combat:damage` |
 * | ActionGameplayEventBlock.h | `gameplay:event` |
 * | ActionCameraBlock.h | `presentation:camera` |
 * | ActionAudioBlock.h | `presentation:audio`, `presentation:audio-state` |
 * | ActionVfxBlock.h | `presentation:vfx`, `presentation:vfx-state` |
 * | ActionAttackVfxBlock.h | `presentation:attack-vfx*` |
 * | ActionPrefabBlock.h | `gameplay:prefab-spawn` |
 * | ActionStateWindowBlock.h | combat/input/collision windows |
 * | ActionParameterCurve.h | `presentation:parameter-curve` |
 *
 * Runtime routing is `ActionNotifyRegistry` / `ActionNotifyPlayback`, not these
 * contract headers.
 */

#include "action/ActionAttackVfxBlock.h"
#include "action/ActionAudioBlock.h"
#include "action/ActionCameraBlock.h"
#include "action/ActionDamageBlock.h"
#include "action/ActionGameplayEventBlock.h"
#include "action/ActionParameterCurve.h"
#include "action/ActionPrefabBlock.h"
#include "action/ActionSpatialBlock.h"
#include "action/ActionStateWindowBlock.h"
#include "action/ActionVfxBlock.h"
