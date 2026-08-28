#include "ui/ScenePanel.h"

#include "common/Capability.h"
#include "common/SceneQuery.h"
#include "ui/UIHost.h"

#include <algorithm>
#include <cstdlib>

namespace eve::ui {
namespace {

constexpr const char* kSceneHostName = "eve_scene";

}  // namespace

ScenePanel::~ScenePanel() {
    if (auto host = UIHost::resolve(host_)) host->get().setTree(window("", {}));
}

void ScenePanel::open() {
    auto host = UIHost::resolve(host_);
    if (!host) {
        host_ = UIHost::createHost(kSceneHostName);
        host  = UIHost::resolve(host_);
    }
    if (!host) return;
    host->get().setVisible(true);
    host->get().setLayer(80);
    refresh();
}

void ScenePanel::close() {
    if (auto host = UIHost::resolve(host_)) host->get().setVisible(false);
}

bool ScenePanel::isOpen() const {
    auto host = UIHost::resolve(host_);
    return host && host->get().meta()->visible;
}

void ScenePanel::refresh() {
    if (auto host = UIHost::resolve(host_)) host->get().setTree(build());
}

bool ScenePanel::selectNode(const std::string& id) {
    eve::ISceneQuery* scene = eve::cap::query<eve::ISceneQuery>();
    if (!scene || id.empty()) return false;
    eve::SceneNodeInfo info;
    if (!scene->getNode(id, &info)) return false;
    selectedId_ = id;
    rebuildHost();
    return true;
}

void ScenePanel::setPickHandler(std::function<void(const std::string&)> handler) {
    pickHandler_ = std::move(handler);
}

WidgetDesc ScenePanel::nodeTree(const eve::SceneNodeInfo& node,
                                const std::vector<eve::SceneNodeInfo>& all) {
    std::vector<WidgetDesc> children;
    children.push_back(button("select " + node.name + "##sel_" + node.id,
                              "sel_" + node.id,
                              [this, id = node.id]() { selectNode(id); }));
    for (const std::string& childId : node.children) {
        const auto found =
            std::find_if(all.begin(), all.end(),
                         [&](const eve::SceneNodeInfo& n) { return n.id == childId; });
        if (found == all.end()) continue;
        children.push_back(nodeTree(*found, all));
    }
    return collapsingHeader(
        node.name + "##hdr_" + node.id, std::move(children), "node_" + node.id,
        false);
}

WidgetDesc ScenePanel::build() {
    std::vector<WidgetDesc> children;
    eve::ISceneQuery* scene = eve::cap::query<eve::ISceneQuery>();
    if (!scene) {
        children.push_back(text(
            "Scene module unavailable. This build has no scene support.",
            "scene_missing"));
        return window("Scene", std::move(children), "root");
    }

    const std::vector<eve::SceneNodeInfo> nodes = scene->nodes(512);
    const std::string hostName = scene->activeHost();
    children.push_back(row(
        {
            text("Scene: " + (hostName.empty() ? std::string("(none)") : hostName),
                 "scene_host"),
            spacer("scene_spacer"),
            button("Refresh##scene_refresh", "scene_refresh",
                   [this]() { refresh(); }),
        },
        "scene_toolbar"));
    children.push_back(separator("scene_sep"));

    // Tree roots (nodes without a parent present in the snapshot).
    for (const eve::SceneNodeInfo& node : nodes) {
        const bool hasParent =
            !node.parent.empty() &&
            std::find_if(nodes.begin(), nodes.end(),
                         [&](const eve::SceneNodeInfo& n) {
                             return n.id == node.parent;
                         }) != nodes.end();
        if (!hasParent) children.push_back(nodeTree(node, nodes));
    }

    // Selected node property sheet.
    if (!selectedId_.empty()) {
        eve::SceneNodeInfo info;
        if (scene->getNode(selectedId_, &info)) {
            const std::string base = "node_" + info.id;
            children.push_back(separator("scene_sel_sep"));
            children.push_back(text("id: " + info.id, base + "_id"));
            children.push_back(text("path: " + info.path, base + "_path"));
            children.push_back(row(
                {
                    text("x", base + "_lx"),
                    inputText("##" + base + "_x", std::to_string(info.x),
                              base + "_x",
                              [id = info.id, scene](const std::string& t) {
                                  eve::SceneNodeInfo cur;
                                  if (!scene->getNode(id, &cur)) return;
                                  cur.x = static_cast<float>(std::atof(t.c_str()));
                                  scene->setNodeTransform(id, cur.x, cur.y, cur.z);
                              }),
                    text("y", base + "_ly"),
                    inputText("##" + base + "_y", std::to_string(info.y),
                              base + "_y",
                              [id = info.id, scene](const std::string& t) {
                                  eve::SceneNodeInfo cur;
                                  if (!scene->getNode(id, &cur)) return;
                                  cur.y = static_cast<float>(std::atof(t.c_str()));
                                  scene->setNodeTransform(id, cur.x, cur.y, cur.z);
                              }),
                    text("z", base + "_lz"),
                    inputText("##" + base + "_z", std::to_string(info.z),
                              base + "_z",
                              [id = info.id, scene](const std::string& t) {
                                  eve::SceneNodeInfo cur;
                                  if (!scene->getNode(id, &cur)) return;
                                  cur.z = static_cast<float>(std::atof(t.c_str()));
                                  scene->setNodeTransform(id, cur.x, cur.y, cur.z);
                              }),
                },
                base + "_transform"));
            children.push_back(row(
                {
                    checkbox("visible##" + base + "_vis", info.visible,
                             base + "_visible",
                             [id = info.id, scene](bool v) {
                                 scene->setNodeVisible(id, v);
                             }),
                    spacer(base + "_vis_spacer"),
                    button("Pick##" + base + "_pick", base + "_pick",
                           [this, id = info.id]() {
                               if (pickHandler_) pickHandler_(id);
                           }),
                },
                base + "_actions"));
        }
    }
    return window("Scene", std::move(children), "root");
}

void ScenePanel::rebuildHost() {
    auto host = UIHost::resolve(host_);
    if (!host || !host->get().meta()->visible) return;
    host->get().setTree(build());
}

}  // namespace eve::ui
