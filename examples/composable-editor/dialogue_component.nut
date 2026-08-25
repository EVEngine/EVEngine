// A project-owned Dialogue editor component. The engine supplies only the
// ConversationDocument data/schema API; projects decide layout and widgets.
class DialogueEditorComponent {
    document = null
    nodeId = "welcome"

    constructor(flow, assetId) {
        document = flow.newDocument(assetId);
        document.addParameter("player");
        document.addNode(nodeId, "line");
        document.setEntry(nodeId);
        document.setField(nodeId, "speaker", "guide");
        document.setField(nodeId, "text", "Welcome, {player.name}.");
        document.setField(nodeId, "next", "end");
    }

    // This project selects two fields for its compact bottom-bar inspector.
    // A graph editor can enumerate every field without changing C++.
    function render(prefix) {
        ui.beginRow(prefix + "fields", 8.0);
        for (local i = 0; i < document.getFieldCount(nodeId); ++i) {
            local key = document.getFieldName(nodeId, i);
            if (key != "speaker" && key != "text") continue;
            ui.beginColumn(prefix + "field-" + key, 2.0);
            ui.text(key + " · " + document.getFieldKind(nodeId, i), prefix + "label-" + key);
            ui.inputText("##" + prefix + key, document.getField(nodeId, key), prefix + key);
            ui.setItemSize(key == "text" ? 260.0 : 130.0, 0.0);
            ui.end();
        }
        ui.end();
    }

    function consumeChange(prefix, controlId) {
        if (controlId.find(prefix) != 0) return false;
        local key = controlId.slice(prefix.len());
        return document.setField(nodeId, key, ui.getValueText(controlId));
    }

    function apply(flow) {
        return document.validate() && flow.applyDocument(document);
    }
}
