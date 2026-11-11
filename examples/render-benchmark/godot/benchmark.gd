extends Node3D

# Matches the EVEngine grid, camera, clear color and one directional light.
# Export a release player and capture its frame presentation externally.
var _started_us := 0
var _last_us := 0
var _samples: PackedFloat64Array = []

func _ready() -> void:
    DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
    Engine.max_fps = 0
    var mesh := BoxMesh.new()
    mesh.size = Vector3.ONE
    var material := StandardMaterial3D.new()
    material.albedo_color = Color(0.65, 0.72, 0.82, 1.0)
    material.metallic = 0.0
    material.roughness = 0.8
    material.shading_mode = BaseMaterial3D.SHADING_MODE_PER_PIXEL
    mesh.material = material
    const GRID := 80
    for z in range(GRID):
        for x in range(GRID):
            var cube := MeshInstance3D.new()
            cube.mesh = mesh
            cube.position = Vector3((x - (GRID - 1) * 0.5) * 1.25, 0.0,
                (z - (GRID - 1) * 0.5) * 1.25)
            cube.scale = Vector3.ONE * 0.8
            add_child(cube)
    var camera := Camera3D.new()
    camera.position = Vector3(0.0, GRID * 1.0, GRID * 1.25)
    add_child(camera)
    camera.look_at(Vector3.ZERO)
    camera.fov = 55.0
    camera.current = true
    var light := DirectionalLight3D.new()
    light.rotation = Vector3(-0.6, -0.4, 0.0)
    add_child(light)
    var environment := WorldEnvironment.new()
    environment.environment = Environment.new()
    environment.environment.background_mode = Environment.BG_COLOR
    environment.environment.background_color = Color(0.08, 0.10, 0.14, 1.0)
    add_child(environment)
    _started_us = Time.get_ticks_usec()
    _last_us = _started_us

func _process(_delta: float) -> void:
    var now := Time.get_ticks_usec()
    var elapsed := float(now - _started_us) / 1000000.0
    if elapsed >= 5.0 and elapsed < 35.0:
        _samples.append(float(now - _last_us) / 1000.0)
    _last_us = now
    if elapsed >= 35.0:
        print("BENCH_RENDER objects=", RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_OBJECTS_IN_FRAME),
            " draws=", RenderingServer.get_rendering_info(RenderingServer.RENDERING_INFO_TOTAL_DRAW_CALLS_IN_FRAME))
        for start in range(0, _samples.size(), 100):
            var line := "BENCH_FRAME_MS"
            for i in range(start, mini(start + 100, _samples.size())):
                line += "," + str(_samples[i])
            print(line)
        print("BENCH_DONE")
        get_tree().quit()
