// This is the configuration for the game
// If not specified, the default values are used
config = {
    // The width of the game
    width = 800
    // The height of the game
    height = 600
    // The title of the game
    title = "game"
    // The debug configuration
    debug = true
    // Soft script/asset hot reload (directory watch)
    hotReload = true
    // Instantiate only these script slots (plus boot: win/gfx/timer/fs/hot/…).
    // Omit `modules` entirely to construct every module the SDK contains.
    modules = ["physics", "particles"]
};
