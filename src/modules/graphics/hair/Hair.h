#pragma once

/**
 * @file Hair.h
 * @brief Umbrella header for the graphics hair / groom subsystem.
 *
 * Phases 1–5 on this branch: strand datas, procedural growth, ribbon expansion,
 * cluster frustum cull, runtime `GroomInstance`, Marschner/self-shadow shading,
 * skin root binding, guide→strand interpolation, and lightweight guide XPBD /
 * Verlet simulation (no hard physics-module include). Geometric Cards LOD mesh
 * builders live in `graphics/HairCards` (separate PR) — do not duplicate them
 * under `graphics/hair/`.
 * See `docs/dev/毛发Groom子系统设计.md`.
 */

#include "graphics/HairShader.h"
#include "graphics/hair/Binding.h"
#include "graphics/hair/ClusterGrid.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Guides.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/Simulation.h"
#include "graphics/hair/StrandsDatas.h"
