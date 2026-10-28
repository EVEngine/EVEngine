// ModelConverter demo config.
config <- {
    width = 1024
    height = 640
    title = "EVEngine Model Converter"
    debug = false
    hotReload = true
};

// Python that has `bpy` installed (Blender's Python package). Try "python"
// if `pip install bpy` was done into the system interpreter.
modelconverter_python <- "python";

// Absolute or game-relative path to the compiled plugin dll/so/dylib.
modelconverter_plugin <- "build/modelconverter.dll";
