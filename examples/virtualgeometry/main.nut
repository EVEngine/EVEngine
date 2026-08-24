// 把 demo.nut 里的 vg_step 挂到标准帧循环（eve_update），
// 让虚拟几何体计算演示可以直接 `make run ... GAME=examples/virtualgeometry` 运行。
dofile("demo.nut");

function eve_update(dt) {
    if ("vg_step" in getroottable())
        vg_step();
}

function eve_render() {
    gfx.clear();
}
