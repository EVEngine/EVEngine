// Runtime projection of the editable ORA masters. PNG paths/order are generated.
dofile("assets/azure-master/manifest.nut");
persist atelier = { avatars = [], textures = [], references = [], pose = 0,
    expression = 0, hair = 0, shirt = 0, jacket = 0, skirt = 0,
    accessory = true, reference = false, baseOnly = false, qa = false,
    layerCount = 0 };
azurePoses <- ["front", "greeting"];
azureExpressions <- ["neutral", "happy", "shy", "angry", "sad", "surprised"];
azureHairs <- ["straight", "waves", "halfup"];
azureShirts <- ["ivory", "blue"];
azureJackets <- ["navy", "ivory", "none"];
azureSkirts <- ["navy", "blue"];

function checkedLayer(result, operation) {
    if (!result) throw "Azure master: " + operation;
}
function createMasterAvatar(pose) {
    local av = avatar.newImageAvatar();
    foreach (entry in azureMasterLayers[pose]) {
        local texture = gfx.newTextureFromFile(entry.file);
        if (texture == null) throw "Missing master export: " + entry.file;
        atelier.textures.append(texture);
        checkedLayer(av.addLayer(entry.name, texture, entry.z), "add " + entry.name);
        checkedLayer(av.setLayerSize(entry.name, 768.0, 1536.0), "size " + entry.name);
    }
    foreach (selected in azureExpressions) {
        local spec = "";
        foreach (entry in azureMasterLayers[pose]) if (entry.slot == "expression") {
            if (spec != "") spec += ";";
            spec += entry.name + "=" + (entry.variant == selected ? "1" : "0");
        }
        checkedLayer(av.defineExpression(selected, spec), "expression " + selected);
    }
    av.setScale(0.52, 0.52); av.setPosition(430.0, 34.0); av.setLayer(10);
    return av;
}
function selectedVariant(slot) {
    if (slot == "body" || slot == "hands" || slot == "face-base") return "base";
    if (slot == "expression") return azureExpressions[atelier.expression];
    if (slot == "hair-back" || slot == "hair-front") return azureHairs[atelier.hair];
    if (slot == "accessory") return atelier.accessory ? "star" : "none";
    if (atelier.baseOnly) return "none";
    local outer = azureJackets[atelier.jacket];
    if (outer in azureMasterOcclusions && azureMasterOcclusions[outer].find(slot) != null) return "none";
    if (slot == "shirt" || slot == "shirt-back") return azureShirts[atelier.shirt];
    // Long jacket sleeves fully occlude the blouse sleeves; the blouse artwork
    // remains complete in the ORA and is revealed by the bolero/no-jacket choice.
    if (slot == "shirt-sleeves") return azureShirts[atelier.shirt];
    if (slot == "jacket" || slot == "jacket-back") return azureJackets[atelier.jacket];
    if (slot == "skirt") return azureSkirts[atelier.skirt];
    throw "Unrecognized master slot: " + slot;
}
function matchingLook() {
    if (atelier.baseOnly || atelier.expression != 0 || !atelier.accessory) return -1;
    if (atelier.shirt == 0 && atelier.jacket == 0 && atelier.skirt == 0 && atelier.hair == 0) return 0;
    if (atelier.shirt == 1 && atelier.jacket == 1 && atelier.skirt == 1 && atelier.hair == 2) return 1;
    return -1;
}
function refreshLabels() {
    ui.select("azure");
    ui.setText("selection", ["静立", "挥手"][atelier.pose] + " · " +
        ["平静", "微笑", "害羞", "生气", "难过", "惊讶"][atelier.expression]);
    ui.setText("hair-state", "当前：" + ["长直发", "微卷", "半扎发"][atelier.hair]);
    ui.setChecked("base", atelier.baseOnly); ui.setChecked("reference", atelier.reference);
    ui.setEnabled("reference", matchingLook() >= 0);
    ui.select("wardrobe");
    ui.setText("shirt-state", "当前衬衣：" + ["象牙白", "浅蓝"][atelier.shirt]);
    ui.setText("jacket-state", "当前上衣：" + ["海军蓝外套", "刺绣披肩", "不穿外套"][atelier.jacket]);
    ui.setText("skirt-state", "当前下裙：" + ["百褶短裙", "轻纱长裙"][atelier.skirt]);
    ui.setChecked("accessory", atelier.accessory);
}
function applyMaster() {
    if (matchingLook() < 0) atelier.reference = false;
    for (local i = 0; i < atelier.avatars.len(); ++i) {
        local av = atelier.avatars[i];
        av.setVisible(i == atelier.pose && !atelier.reference);
        foreach (entry in azureMasterLayers[azurePoses[i]])
            checkedLayer(av.setLayerVisible(entry.name, selectedVariant(entry.slot) == entry.variant), entry.name);
        checkedLayer(av.applyExpression(azureExpressions[atelier.expression]), "apply expression");
    }
    refreshLabels();
}
function masterRow(title, prefix, labels) {
    ui.text(title, prefix + "-title"); ui.beginRow(prefix + "-row", 8.0);
    for (local i = 0; i < labels.len(); ++i) ui.button(labels[i], prefix + i);
    ui.end();
}
function mountMasterUi() {
    ui.beginBuild(); ui.beginWindow("澄蓝 · 分层母稿", "root");
    ui.text("AZURE / MASTER ATELIER", "subtitle");
    ui.text("静立 · 平静", "selection"); ui.separator("s0");
    masterRow("姿势", "pose", ["静立", "挥手"]);
    masterRow("表情", "expression", ["平静", "微笑", "害羞"]);
    ui.beginRow("expressions-more", 8.0); ui.button("生气", "expression3");
    ui.button("难过", "expression4"); ui.button("惊讶", "expression5"); ui.end();
    masterRow("发型", "hair", ["长直发", "微卷", "半扎发"]);
    ui.text("", "hair-state"); ui.separator("s1");
    ui.checkbox("查看完整身体底稿", false, "base");
    ui.checkbox("显示母稿完成图", false, "reference");
    ui.textWrapped("选择右侧整套搭配后，可切换完成图检查重组效果。", 260.0, "compare-help");
    ui.textWrapped("身体、衣领、衣袖与前景手部均保留为独立绘制层。", 260.0, "master-help");
    ui.end(); ui.mountBuildAs("azure"); ui.select("azure");
    ui.setHostPos(22.0, 28.0, 0.0, 0.0); ui.setHostSize(300.0, 810.0); ui.setHostOverlay(false);
    ui.beginBuild(); ui.beginWindow("服装与搭配", "root");
    masterRow("衬衣", "shirt", ["象牙白", "浅蓝"]); ui.text("", "shirt-state");
    masterRow("上衣", "jacket", ["海军蓝外套", "刺绣披肩"]); ui.button("不穿外套", "jacket2");
    ui.text("", "jacket-state");
    masterRow("下裙", "skirt", ["百褶短裙", "轻纱长裙"]); ui.text("", "skirt-state");
    ui.checkbox("银星发饰", true, "accessory"); ui.separator("s2");
    masterRow("整套搭配", "look", ["学院", "轻礼装"]);
    ui.textWrapped("上衣、下裙、衬衣均可独立更换。", 260.0, "mix-help");
    ui.end(); ui.mountBuildAs("wardrobe"); ui.select("wardrobe");
    ui.setHostPos(950.0, 28.0, 0.0, 0.0); ui.setHostSize(310.0, 810.0); ui.setHostOverlay(false);
}
function chooseMasterLook(look) {
    atelier.shirt = look; atelier.jacket = look; atelier.skirt = look;
    atelier.hair = look == 0 ? 0 : 2; atelier.expression = 0;
    atelier.accessory = true; atelier.baseOnly = false; applyMaster();
}
function masterClick(path) {
    local slash = path.find("/"); if (slash == null) return;
    local id = path.slice(slash + 1);
    foreach (key in ["pose", "expression", "hair", "shirt", "jacket", "skirt"]) {
        if (id.find(key) == 0) {
            local value = id.slice(key.len()).tointeger();
            local limit = key == "pose" || key == "shirt" || key == "skirt" ? 2 : (key == "expression" ? 6 : 3);
            if (value < 0 || value >= limit) throw "Invalid selection: " + id;
            atelier[key] = value; applyMaster(); return;
        }
    }
    if (id == "look0" || id == "look1") chooseMasterLook(id == "look0" ? 0 : 1);
}
eve_init = function() {
    gfx.setBackgroundColor(0.91, 0.93, 0.96, 1.0);
    // Instances/textures are retained across soft reload; never create duplicates.
    if (atelier.avatars.len() == 0) {
        avatar = eve.Avatar();
        foreach (pose in azurePoses) {
            atelier.avatars.append(createMasterAvatar(pose));
            local images = [];
            foreach (look in ["academy", "formal"]) {
                local tex = gfx.newTextureFromFile(azureMasterReferenceRoot + pose + "/complete-" + look + ".png");
                if (tex == null) throw "Missing master reference: " + pose + look;
                images.append(tex);
            }
            atelier.references.append(images);
        }
        atelier.layerCount = atelier.avatars[0].getLayerCount();
    }
    ui.setTheme("dark"); ui.setNavKeyboard(true); mountMasterUi(); applyMaster();
    print("azure-master: revision=" + azureMasterRevision + " layers-per-pose=" + atelier.layerCount + "\n");
};
eve_update = function(dt) {
    local click = ui.consumeClick();
    while (click != "") { masterClick(click); click = ui.consumeClick(); }
    local change = ui.consumeChange();
    while (change != "") {
        if (change == "azure/base") { ui.select("azure"); atelier.baseOnly = ui.getChecked("base"); }
        if (change == "azure/reference") { ui.select("azure"); atelier.reference = ui.getChecked("reference"); }
        if (change == "wardrobe/accessory") { ui.select("wardrobe"); atelier.accessory = ui.getChecked("accessory"); }
        applyMaster(); change = ui.consumeChange();
    }
    avatar.update(dt);
};
eve_render = function() {
    gfx.clear();
    if (atelier.reference && matchingLook() >= 0)
        gfx.drawTexturedRect(atelier.references[atelier.pose][matchingLook()], 430.0, 34.0, 399.36, 798.72,
                             1.0, 1.0, 1.0, 1.0);
    else avatar.render(gfx);
    if (!atelier.qa) ui.beginFrameAndRender();
};
