// scene_director.nut — EVEngine scene-authoring kit (AI / agent drivable).
//
// Installs `::scene_director` into the live VM roottable. The engine MCP tools
// (`eve_scene_modify`, `eve_scene_info`, `eve_camera_generate`,
// `eve_scene_reset`) and the Python agents (creative-brain / scene-qc /
// story-scene-agent) drive scene construction exclusively through this kit:
//
//   spawn / move / scale / rotate / remove / set_visible / set_material
//   set_lighting / camera / cameras / info / list / reset / modify / status
//
// State is tracked locally (Renderable3D has no transform getters), so `info()`
// and `list()` always report the authoritative agent-visible transform.
//
// NOTE: every cross-reference is written as `scene_director.<field>` because a
// Squirrel table stores anonymous closures whose `this` may not be searched for
// bare identifiers.
//
// Install (idempotent) — called automatically by the MCP tools, or manually:
//   eve_run_script  source: "scene_director_install();"
// A host game can also load it at boot (see examples/ai-stage):
//   compilestring(eve.sceneDirectorScript)();

persist scene_director = null

// ---- internal helpers (roottable scope; reused across reinstalls) ----

function sd_num(v, fallback) {
    if (typeof v == "integer" || typeof v == "float") return v;
    return fallback;
}

function sd_int(v, fallback) {
    if (typeof v == "integer") return v;
    if (typeof v == "float") return v.tointeger();
    return fallback;
}

function sd_arr3(v, def) {
    local out = [def[0], def[1], def[2]];
    if (typeof v == "array") {
        for (local i = 0; i < 3 && i < v.len(); i++) out[i] = sd_num(v[i], def[i]);
    }
    return out;
}

function sd_recipe_from_kind(kind) {
    local key = kind;
    if (key.len() > 5 && key.slice(0, 5) == "mesh.") key = key.slice(5);
    return key;
}

// Build a Mesh for a prop kind. Returns Mesh or null (unknown kind).
function sd_build_mesh(kind, seed, mesh_params) {
    if (!("gfx" in getroottable())) return null;
    local key = sd_recipe_from_kind(kind);
    if (key == "box" || key == "cube") return gfx.newMeshCube(1.0);
    if (key == "sphere" || key == "ball") return gfx.newMeshSphere(24, 12);
    if (key == "cylinder" || key == "pillar" || key == "column" || key == "trunk")
        return gfx.newMeshCylinder(16, 1, true);
    // Procedural recipe: tree / rock / bush / skyscraper / hexplanet /
    // marchingcubes / fence / stonewall / bridge / greatwall / hedge / chevaldefrise
    if ("procgen" in getroottable()) {
        local recipe = "mesh." + key;
        try {
            if (procgen.hasMeshRecipe(recipe)) {
                local p = procgen.newParams();
                p.setSeed(seed);
                p.setFloat("scale", 1.0);
                if (typeof mesh_params == "table") {
                    foreach (k, v in mesh_params) {
                        local tk = typeof v;
                        if (tk == "integer") p.setInt(k, v);
                        else if (tk == "float") p.setFloat(k, v);
                        else if (tk == "string") p.setString(k, v);
                    }
                }
                return procgen.generateMesh(recipe, p, gfx);
            }
        } catch (e) {
            return null;
        }
    }
    return null;
}

// Scene bounds: center + radius from all staged props (fallback radius 10).
function sd_bounds() {
    local props = scene_director.props;
    local n = props.len();
    if (n == 0) return { cx = 0.0, cy = 1.0, cz = 0.0, r = 10.0 };
    local minx = 1e9, miny = 1e9, minz = 1e9;
    local maxx = -1e9, maxy = -1e9, maxz = -1e9;
    foreach (p in props) {
        if (p.x < minx) minx = p.x;
        if (p.y < miny) miny = p.y;
        if (p.z < minz) minz = p.z;
        if (p.x > maxx) maxx = p.x;
        if (p.y > maxy) maxy = p.y;
        if (p.z > maxz) maxz = p.z;
    }
    local cx = (minx + maxx) * 0.5;
    local cy = (miny + maxy) * 0.5;
    local cz = (minz + maxz) * 0.5;
    local r = 0.5 * (maxx - minx + maxz - minz);
    if (r < 4.0) r = 4.0;
    return { cx = cx, cy = cy, cz = cz, r = r };
}

// ---- public kit (installed once; function fields re-bound on reinstall) ----

if (scene_director == null) {
    scene_director <- {
        props = [],          // [{id, kind, x, y, z, sx, sy, sz, yaw_deg, tint, seed, ent}]
        cameraObj = null,    // live Camera3D instance; `camera` stays the kit function
        sprites = {},
        _installed = false,
    };
}

scene_director.install <- function() {
    scene_director._installed = true;
    return true;
};

scene_director.reset <- function() {
    scene_director.props.clear();
    if (scene_director.cameraObj != null) {
        try { scene_director.cameraObj.setActive(false); } catch (e) {}
        scene_director.cameraObj = null;
    }
    if ("gfx" in getroottable()) {
        gfx.setBackgroundColor(0.10, 0.13, 0.19, 1.0);
        gfx.setDirectionalLight(-0.5, -1.0, -0.4, 1.35, 1.25, 1.05);
    }
    return "ok";
};

scene_director.find <- function(id) {
    foreach (p in scene_director.props) if (p.id == id) return p;
    return null;
};

scene_director.spawn <- function(params) {
    local id = ("id" in params) ? params.id : ("prop_" + scene_director.props.len());
    if (scene_director.find(id) != null) return { ok = false, error = "id already exists: " + id };
    local kind = ("kind" in params) ? params.kind : "box";
    local seed = ("seed" in params) ? sd_int(params.seed, 0) : (scene_director.props.len() * 131 + 7);
    local pos = sd_arr3(params.pos, [0.0, 0.5, 0.0]);
    local scl = sd_arr3(params.scale, [1.0, 1.0, 1.0]);
    local yawDeg = ("yaw_deg" in params) ? params.yaw_deg : 0.0;
    if (kind == "ground" || kind == "floor") scl = [scl[0], 0.06, scl[2]];

    local mesh = sd_build_mesh(kind, seed, ("mesh_params" in params) ? params.mesh_params : null);
    if (mesh == null) return { ok = false, error = "unknown kind or recipe: " + kind };

    local ent = eve.Renderable3D();
    ent.setMesh(mesh);
    ent.setPosition(pos[0], pos[1], pos[2]);
    ent.setScale(scl[0], scl[1], scl[2]);
    if (yawDeg != 0.0) ent.setYaw(yawDeg * 0.0174533);

    local tint = ("tint" in params) ? sd_arr3(params.tint, [1.0, 1.0, 1.0]) : [1.0, 1.0, 1.0];
    ent.setTint(tint[0], tint[1], tint[2], 1.0);
    ent.setRoughness(("roughness" in params) ? params.roughness : 0.75);
    ent.setMetallic(("metallic" in params) ? params.metallic : 0.02);
    ent.setCastShadow(("cast_shadow" in params) ? params.cast_shadow : true);
    ent.setReceiveShadow(("receive_shadow" in params) ? params.receive_shadow : true);
    if ("visible" in params) ent.setVisible(params.visible);

    scene_director.props.append({
        id = id, kind = kind, seed = seed,
        x = pos[0], y = pos[1], z = pos[2],
        sx = scl[0], sy = scl[1], sz = scl[2],
        yaw_deg = yawDeg, tint = tint, ent = ent,
    });
    return { ok = true, id = id, kind = kind };
};

scene_director.move <- function(id, x, y, z) {
    local p = scene_director.find(id);
    if (p == null) return { ok = false, error = "unknown id: " + id };
    p.x = x; p.y = y; p.z = z;
    p.ent.setPosition(x, y, z);
    return { ok = true, id = id };
};

scene_director.scale <- function(id, sx, sy, sz) {
    local p = scene_director.find(id);
    if (p == null) return { ok = false, error = "unknown id: " + id };
    if (p.kind == "ground" || p.kind == "floor") sy = 0.06;
    p.sx = sx; p.sy = sy; p.sz = sz;
    p.ent.setScale(sx, sy, sz);
    return { ok = true, id = id };
};

scene_director.rotate <- function(id, yaw_deg) {
    local p = scene_director.find(id);
    if (p == null) return { ok = false, error = "unknown id: " + id };
    p.yaw_deg = yaw_deg;
    p.ent.setYaw(yaw_deg * 0.0174533);
    return { ok = true, id = id };
};

scene_director.set_visible <- function(id, v) {
    local p = scene_director.find(id);
    if (p == null) return { ok = false, error = "unknown id: " + id };
    p.ent.setVisible(v);
    return { ok = true, id = id };
};

scene_director.set_material <- function(id, params) {
    local p = scene_director.find(id);
    if (p == null) return { ok = false, error = "unknown id: " + id };
    if ("tint" in params) {
        local t = sd_arr3(params.tint, [1.0, 1.0, 1.0]);
        p.tint = t;
        p.ent.setTint(t[0], t[1], t[2], 1.0);
    }
    if ("roughness" in params) p.ent.setRoughness(params.roughness);
    if ("metallic" in params) p.ent.setMetallic(params.metallic);
    return { ok = true, id = id };
};

scene_director.remove <- function(id) {
    for (local i = 0; i < scene_director.props.len(); i++) {
        if (scene_director.props[i].id == id) {
            scene_director.props.remove(i);
            return { ok = true, id = id };
        }
    }
    return { ok = false, error = "unknown id: " + id };
};

scene_director.set_lighting <- function(params) {
    local tod = ("timeOfDay" in params) ? params.timeOfDay : "day";
    local intensity = ("intensity" in params) ? params.intensity : 1.0;
    local bg = [0.10, 0.13, 0.19];
    if (tod == "day") bg = [0.52, 0.58, 0.70];
    else if (tod == "dusk") bg = [0.34, 0.26, 0.38];
    else if (tod == "dawn") bg = [0.46, 0.42, 0.52];
    else if (tod == "night") bg = [0.05, 0.06, 0.11];
    if ("background" in params) bg = sd_arr3(params.background, bg);
    if ("gfx" in getroottable()) {
        gfx.setBackgroundColor(bg[0], bg[1], bg[2], 1.0);
        gfx.setDirectionalLight(-0.5, -1.0, -0.4,
                                intensity, intensity * 0.95, intensity * 0.85);
    }
    if (scene_director.cameraObj != null) {
        scene_director.cameraObj.setAmbient(0.16 * intensity, 0.18 * intensity, 0.22 * intensity);
    }
    return { ok = true, timeOfDay = tod };
};

scene_director.camera <- function(params) {
    local eye = ("eye" in params) ? sd_arr3(params.eye, [0.0, 6.0, 12.0]) : [0.0, 6.0, 12.0];
    local tgt = ("target" in params) ? sd_arr3(params.target, [0.0, 1.0, 0.0]) : [0.0, 1.0, 0.0];
    local fov = ("fov" in params) ? params.fov : 55.0;
    if (scene_director.cameraObj == null) scene_director.cameraObj = eve.Camera3D();
    scene_director.cameraObj.setEye(eye[0], eye[1], eye[2]);
    scene_director.cameraObj.setTarget(tgt[0], tgt[1], tgt[2]);
    scene_director.cameraObj.setUp(0.0, 1.0, 0.0);
    scene_director.cameraObj.setFov(fov);
    scene_director.cameraObj.setAmbient(0.20, 0.22, 0.26);
    scene_director.cameraObj.setActive(true);
    return { ok = true, eye = eye, target = tgt, fov = fov };
};

// Standardized QC camera rigs around the staged scene.
// Uses a fixed 6-way compass table (no math-lib dependency), so it works both
// in the engine (eve.Math) and in plain ssq test VMs (standard Squirrel math).
scene_director.cameras <- function(count) {
    local n = sd_int(count, 6);
    if (n < 1) n = 1;
    local cosT = [1.0, 0.5, -0.5, -1.0, -0.5, 0.5];          // cos(60° * i)
    local sinT = [0.0, 0.8660254, 0.8660254, 0.0, -0.8660254, -0.8660254];
    local b = sd_bounds();
    local out = [];
    for (local i = 0; i < n; i++) {
        local k = i % 6;
        local dist = b.r * 1.8;
        local height = b.cy + 3.0 + (i % 2) * 2.5;           // varying elevation
        out.append({
            id = "cam_" + i,
            eye = [b.cx + cosT[k] * dist, height, b.cz + sinT[k] * dist],
            target = [b.cx, b.cy, b.cz],
            fov = 55.0,
        });
    }
    return out;
};

scene_director.info <- function() {
    local items = [];
    foreach (p in scene_director.props) {
        items.append({
            id = p.id, kind = p.kind,
            pos = [p.x, p.y, p.z],
            scale = [p.sx, p.sy, p.sz],
            yaw_deg = p.yaw_deg,
            tint = p.tint,
        });
    }
    return { count = scene_director.props.len(), props = items };
};

scene_director.list <- function() {
    local ids = [];
    foreach (p in scene_director.props) ids.append(p.id);
    return ids;
};

scene_director.status <- function() {
    return {
        installed = true,
        propCount = scene_director.props.len(),
        hasCamera = (scene_director.cameraObj != null),
    };
};

// Generic dispatcher used by MCP `eve_scene_modify` and the QC fixer.
scene_director.modify <- function(action, target, params) {
    if (params == null) params = {};
    if (action == "reset") return scene_director.reset();
    if (action == "add_object" || action == "spawn" || action == "place")
        return scene_director.spawn(params);
    if (action == "move_object" || action == "move") {
        if (("x" in params) && ("y" in params) && ("z" in params))
            return scene_director.move(target, params.x, params.y, params.z);
        if ("pos" in params) {
            local p = sd_arr3(params.pos, [0.0, 0.0, 0.0]);
            return scene_director.move(target, p[0], p[1], p[2]);
        }
        return { ok = false, error = "move needs x/y/z or pos" };
    }
    if (action == "scale") {
        if (("sx" in params) && ("sy" in params) && ("sz" in params))
            return scene_director.scale(target, params.sx, params.sy, params.sz);
        if ("scale" in params) {
            local s = sd_arr3(params.scale, [1.0, 1.0, 1.0]);
            return scene_director.scale(target, s[0], s[1], s[2]);
        }
        return { ok = false, error = "scale needs sx/sy/sz or scale" };
    }
    if (action == "rotation" || action == "rotate")
        return scene_director.rotate(target, ("yaw_deg" in params) ? params.yaw_deg : params.yaw);
    if (action == "remove_object" || action == "remove") return scene_director.remove(target);
    if (action == "visibility" || action == "visible")
        return scene_director.set_visible(target, params.visible);
    if (action == "material") return scene_director.set_material(target, params);
    if (action == "lighting" || action == "set_lighting") return scene_director.set_lighting(params);
    if (action == "camera") return scene_director.camera(params);
    if (action == "cameras") return scene_director.cameras(("count" in params) ? params.count : 6);
    if (action == "info") return scene_director.info();
    if (action == "list") return scene_director.list();
    return { ok = false, error = "unknown action: " + action };
};

// Helper the examples / host game can call from boot scripts.
function scene_director_install() {
    if (!("scene_director" in getroottable())) return false;
    return scene_director.install();
}
