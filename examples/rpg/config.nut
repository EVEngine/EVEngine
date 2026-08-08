// examples/rpg 的窗口配置。
// 约定见 src/scripts/load.nut：eve run 会先 dofile("config.nut") 再 dofile("main.nut")。
config = {
    width = 960
    height = 640
    title = "EVEngine RPG 示例 —— 地牢生存"
    debug = false
    // 改脚本自动软重载（仅重跑 main.nut，见 main.nut 顶部的状态保护写法）。
    hotReload = true
};
