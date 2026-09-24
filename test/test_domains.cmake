# Test-domain table: the single source of truth for how the globbed test/*.cpp
# set is partitioned into link units (unit_test_<domain>).
#
# Why this exists: the suite used to link every test translation unit into one
# `unit_test` binary. That single link unit reached 789 sources / 5188 cases and
# forced MSVC incremental linking to keep the relink tolerable, which cost ~2.3 GB
# of .ilk plus a ~1.8 GB .pdb for the test binary alone. Splitting the suite into
# per-domain executables removes the need for incremental linking and lets an
# agent link only the domain it is testing.
#
# Classification is derived from the test's own includes, not its file name: the
# first directory segment of a `#include "<segment>/..."` that names a module
# package root under src/modules decides the domain. Include-based classification
# is authoritative for the cases where the file name lies -- test/pcg_*.cpp are
# EVUI photo-mode tests (they include ui/...), and test/action_*.cpp splits
# between the combat `action` module and the editor action timeline.
#
# Files with no module include (script-binding harnesses, fixture-driven suites
# such as rts_*/procgen_cave*, and core utility tests) fall back to a basename
# prefix table below.
#
# Invariants, enforced at configure time by test/CMakeLists.txt and without
# configuring by scripts/check_test_manifest.py:
#   * every globbed test source lands in exactly one domain;
#   * a source matching no rule, or two rules, is a hard error that names the
#     file -- so a new test cannot silently join the wrong link unit;
#   * test/main.cpp is never a domain member: it provides the runner entry point
#     and is compiled into every domain executable.
#
# Row format: "<module package root>;<domain>". Root is the first path segment of
# the module header include, i.e. the directory name under src/modules.

set(EVE_TEST_DOMAINS
    core
    scripts
    graphics
    ui
    editor
    scene
    animation
    physics
    fluids
    procgen
    map
    voxel
    particles
    rpg
    combat
    tactics_rts
    climbing
    card
    npc_ai
    building
    dialogue
    asset
    audio
    platform
    weather
    tensor
    agent
    pixelworld
    network
    devtools
    CACHE INTERNAL "unit_test_<domain> link units, in build order")

# Module package root -> domain. Roots come from src/modules/*; facets map to
# their host (src/modules/graphics/material is `material`, src/modules/map/level
# is `level`) exactly as cmake/modules.cmake:eve_package_root() does.
set(EVE_TEST_MODULE_DOMAIN
    # --- core: foundation, data, containers, resources, binding infrastructure
    "math;core"
    "data;core"
    "filesystem;core"
    "schema;core"
    "tags;core"
    "transaction;core"
    "spatial;core"
    "ik;core"
    "grid;core"
    "timer;core"
    "os;core"
    "thread;core"
    "property_access;core"
    "statepatch;core"
    "profiler;core"
    "platform_event;core"
    "plugins;core"
    "database;core"
    "i18n;core"
    "image;core"
    "font;core"
    "rx;core"

    # --- scripts: Squirrel runtime and dnut
    "dnut_interpreter;scripts"

    # --- graphics: renderer plus every render-feature module
    "graphics;graphics"
    "gpgpu;graphics"
    "camera;graphics"
    "effects;graphics"
    "decal;graphics"
    "spritestack;graphics"
    "hd2d;graphics"
    "virtualgeometry;graphics"
    "stylize;graphics"
    "model3d;graphics"
    "demo;graphics"

    # --- ui
    "ui;ui"

    # --- editor: the authoring package roots. Authoring facets (material,
    # lighting, level, input, queue, biome, domain_gizmo) have no directory of
    # their own -- their headers live under the host package -- so they are
    # reached through EVE_TEST_PACKAGE_OVERRIDE below, not through this table.
    "editor;editor"
    "editing;editor"

    # --- scene graph and loading (scene/loader is reached via the `scene` root)
    "scene;scene"

    # --- animation and avatars
    "animation;animation"
    "avatar;animation"

    # --- physics and vehicles
    "physics;physics"
    "vehicle;physics"

    # --- simulation-heavy domains
    "fluids;fluids"
    "procgen;procgen"
    "map;map"
    "hexmap;map"
    "voxel;voxel"
    "particles;particles"
    "pixelworld;pixelworld"

    # --- rpg layer: attributes/definitions/economy and their consumers
    "rpg;rpg"
    "inventory;rpg"
    "economy;rpg"
    "attributes;rpg"
    "definitions;rpg"
    "authority;rpg"
    "decision;rpg"
    "production;rpg"
    "orders;rpg"
    "settlement;rpg"
    "social;rpg"
    "policyregistry;rpg"
    "game_event;rpg"

    # --- combat: action/weapon/combat
    "combat;combat"
    "weapon;combat"
    "action;combat"

    # --- turn-based and strategy
    "tactics;tactics_rts"
    "rts;tactics_rts"
    "card;card"

    # --- traversal and ai
    "climbing;climbing"
    "npc_ai;npc_ai"
    "crowd;npc_ai"
    "sensing;npc_ai"

    # --- construction
    # house generation lives under the procgen package (src/modules/procgen/house)
    # and its tests include procgen/... headers, so they classify with `procgen`
    # through the include rule above rather than through a root of their own.
    "building;building"
    "archspace;building"

    # --- narrative, assets, media, platform, weather
    "dialogue;dialogue"
    "asset;asset"
    "audio;audio"
    "sound;audio"
    "mouse;platform"
    "keyboard;platform"
    "joystick;platform"
    "touch;platform"
    "window;platform"
    "weather;weather"
    "daynight;weather"

    # --- ml and search
    "tensor;tensor"
    "agent;agent"

    # --- networking
    "network;network"
    CACHE INTERNAL "Test module package root -> domain")

# Authoring facets whose headers sit under a host package. Classifying by the
# include's first segment alone would send these to the host's domain; the two
# segment path below wins instead. Only entries whose domain differs from the
# host's are listed -- map/level holds EVLevel_editing and EVLevel_editor, which
# belong with the editor link unit rather than with `map`.
# Rows are "<root>/<facet>;<domain>".
set(EVE_TEST_PACKAGE_OVERRIDE
    "map/level;editor"
    "agent/tensor;tensor"
    CACHE INTERNAL "Two-segment module package path -> domain")

# Default when a test's includes name no module package root: script-binding
# harnesses, fixture-driven suites, and core utility tests. Rows are
# "<prefix>;<domain>" for a basename prefix match, or "=<name>;<domain>" for an
# exact basename match (needed where one name is a prefix of another rule's name,
# e.g. `runtime` vs `runtime_handle` / `runtime_error_sink`). The longest match
# wins; two equally long matches in different domains is an error.
set(EVE_TEST_PREFIX_DOMAIN
    "binding_selfcheck;core"
    "capability;core"
    "cmdline;core"
    "common_;core"
    "crashlog;core"
    "diagnostic_value;core"
    "ECS;core"
    "filesystem;core"
    "i18n_script;core"
    "image_script;core"
    "json;core"
    "medialoader_model_link;core"
    "=model;core"
    "module_lazy_bind;core"
    "profiler_script;core"
    "resource;core"
    "result;core"
    "runtime_handle;core"
    "time_contract;core"
    "timer;core"
    "value;core"
    "versioned_registry;core"

    "ReflectScript;scripts"
    "=runtime;scripts"
    "script_;scripts"
    "ScriptECS;scripts"
    "simplesquirrel;scripts"
    "squirrel_binding;scripts"

    "decal_script;graphics"
    "graphic;graphics"
    "graphics_primitive_script;graphics"

    "editor_;editor"
    "scene_editor;editor"

    "procgen_cave;procgen"
    "procgen_schema_script;procgen"

    "voxel_render;voxel"

    "composable_gameplay;rpg"
    "gameplay_control_json;rpg"
    "rpg_script;rpg"
    "state_value;rpg"

    "climbing_playground;climbing"
    "crowd_script;npc_ai"
    "dialogue_proc_script;dialogue"
    "rts_;tactics_rts"
    "tactics_script;tactics_rts"

    "joystick_script;platform"
    "mouse;platform"
    "window;platform"

    "agent_development_session;devtools"
    "callgraph;devtools"
    "console;devtools"
    "dap;devtools"
    "debugger;devtools"
    "language_;devtools"
    "mcp;devtools"
    "play_;devtools"
    "protocol_dispatch;devtools"
    "reload_script;devtools"
    "renderflow;devtools"
    "runtime_error_sink;devtools"
    CACHE INTERNAL "Test basename prefix -> domain, for sources with no module include")
