// iOS shell config. width/height 0 => use the device display size.
// No main.nut here → load.nut runs embedded eve.demoScript (meteor + particles).
config = {
    width = 0
    height = 0
    title = "EVEngine Demo"
    debug = true
    // Remote hot reload: point at your dev machine's `eve dev` server, e.g.
    // devServer = "http://192.168.1.5:8765"
    devServer = ""
};
