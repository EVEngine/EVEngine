#pragma once

/**
 * @file Hair.h
 * @brief Umbrella header for the graphics hair / groom subsystem.
 *
 * Phase 1: strand datas, procedural growth, ribbon expansion, and a runtime
 * `GroomInstance` drawn through the existing Kajiya-Kay hair shader.
 * See `docs/dev/毛发Groom子系统设计.md`.
 */

#include "graphics/HairShader.h"
#include "graphics/hair/GroomAsset.h"
#include "graphics/hair/GroomInstance.h"
#include "graphics/hair/Procedural.h"
#include "graphics/hair/RibbonBuilder.h"
#include "graphics/hair/StrandsDatas.h"
