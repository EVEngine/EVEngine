config = {
    width = 800
    height = 600
    title = "Basic - step by step"
    debug = false
    hotReload = true

    // 1 rectangle, 2 image, 3 movement, 4 physics, 5 particles, 6 fire.
    // Restart after changing this number: the module list is a startup choice.
    tutorialStep = 1
    modules = []
};
if (config.tutorialStep == 3) config.modules.append("keyboard");
if (config.tutorialStep >= 4) config.modules.append("physics");
if (config.tutorialStep >= 5) config.modules.append("particles");
