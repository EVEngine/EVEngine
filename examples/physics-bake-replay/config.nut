config = {
    width = 1280
    height = 800
    title = "Physics Bake Replay | simulate once, replay twice"
    hotReload = false
    modules = ["physics", "cloth", "rope", "particles", "physicsBakeGraphics", "ui", "keyboard"]
    bake = { fixedHz = 60.0, seconds = 3.0, stride = 2, playbackOnly = false }
};
