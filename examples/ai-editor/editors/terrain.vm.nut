// ViewModel（Squirrel 表）：与上面 JSON View 做 MVVM 双向绑定。
// 宿主把表作为 this 调用方法；字段被 View 读写。
::TerrainVM <- {
    brushSize = 12,
    strength = 0.3,
    tool = "Raise",
    wireframe = false,

    // 值变化回调（widgetId, value）。
    onChange = function(widget, value) {
        // 演示：只写日志；真实项目在这里读写引擎数据（Heightmap / Material ...）。
    },

    // 按钮命令回调（editorId, widgetId）。
    apply = function(editor, widget) {
        // 演示：把当前参数写回 View 上的只读文本（见 editor_demo.py 的 set_value）。
    }
};
