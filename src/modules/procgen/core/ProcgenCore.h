#pragma once

/**
 * @brief Backend-neutral procgen surface.
 *
 * This header is limited to owning CPU data, recipe metadata and deterministic
 * build state. Map, scene, graphics and image adapters belong to the public
 * Procgen facade or to their adapter translation units, not to this include set.
 */
#include "procgen/GeneratorRegistry.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/Grid2D.h"
#include "procgen/JsonExport.h"
#include "procgen/MeshBuild.h"
#include "procgen/ParamSchema.h"
#include "procgen/Params.h"
#include "procgen/PointSet.h"
#include "procgen/ProcgenSystem.h"
#include "procgen/Semantic.h"
