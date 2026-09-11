// Minimal deterministic JSON codec for the level document.
//
// The `data` module exposes JsonDocument but not a script-facing get/set surface,
// and the level document must be a plain data file rather than script source, so
// the codec is implemented here. It covers exactly the value shapes the level
// schema uses: objects with string keys, arrays, strings, numbers, booleans and
// null. Numbers round-trip through the VM's own text form.

function levelJsonEscape(text) {
    local out = "";
    for (local i = 0; i < text.len(); ++i) {
        local c = text[i];
        if (c == '"') out += "\\\"";
        else if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else if (c < 32) out += " ";
        else out += c.tochar();
    }
    return out;
}

function levelJsonNumber(value) {
    local text = value.tostring();
    // Only floats need a decimal point; appending one to an integer would make it
    // decode back as a float and break integer schema fields.
    if (typeof value == "float" && text.find(".") == null && text.find("e") == null &&
        text.find("n") == null)
        text += ".0";
    return text;
}

/** @brief Deterministic key order so level diffs stay stable. */
function levelSortedKeys(table) {
    local keys = [];
    foreach (key, item in table) if (typeof key == "string") keys.push(key);
    for (local i = 1; i < keys.len(); ++i) {
        local value = keys[i];
        local j = i - 1;
        while (j >= 0 && keys[j] > value) { keys[j + 1] = keys[j]; j -= 1; }
        keys[j + 1] = value;
    }
    return keys;
}

function levelJsonEncodeValue(value, indent) {
    local pad = "";
    for (local i = 0; i < indent; ++i) pad += "  ";
    local inner = "";
    for (local i = 0; i <= indent; ++i) inner += "  ";
    local type = typeof value;

    if (value == null) return "null";
    if (type == "bool") return value ? "true" : "false";
    if (type == "integer" || type == "float") return levelJsonNumber(value);
    if (type == "string") return "\"" + levelJsonEscape(value) + "\"";

    if (type == "array") {
        if (value.len() == 0) return "[]";
        local parts = [];
        foreach (item in value) parts.push(inner + levelJsonEncodeValue(item, indent + 1));
        return "[\n" + levelJoin(parts, ",\n") + "\n" + pad + "]";
    }
    if (type == "table") {
        local keys = levelSortedKeys(value);
        if (keys.len() == 0) return "{}";
        local parts = [];
        foreach (key in keys)
            parts.push(inner + "\"" + levelJsonEscape(key) + "\": " +
                       levelJsonEncodeValue(value[key], indent + 1));
        return "{\n" + levelJoin(parts, ",\n") + "\n" + pad + "}";
    }
    throw "level document cannot encode a " + type;
}

function levelJoin(parts, separator) {
    local text = "";
    for (local i = 0; i < parts.len(); ++i) text += (i == 0 ? "" : separator) + parts[i];
    return text;
}

function levelJsonEncode(value) { return levelJsonEncodeValue(value, 0) + "\n"; }

// --- parser ---------------------------------------------------------------

function levelJsonParser(text) {
    return {text = text, at = 0};
}

function levelJsonSkip(parser) {
    while (parser.at < parser.text.len()) {
        local c = parser.text[parser.at];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') parser.at += 1;
        else break;
    }
}

function levelJsonFail(parser, message) {
    throw "level document: " + message + " at offset " + parser.at;
}

function levelJsonExpect(parser, expected) {
    levelJsonSkip(parser);
    if (parser.at >= parser.text.len() || parser.text[parser.at] != expected)
        levelJsonFail(parser, "expected '" + expected.tochar() + "'");
    parser.at += 1;
}

function levelJsonParseString(parser) {
    levelJsonExpect(parser, '"');
    local out = "";
    while (parser.at < parser.text.len()) {
        local c = parser.text[parser.at];
        parser.at += 1;
        if (c == '"') return out;
        if (c != '\\') { out += c.tochar(); continue; }
        if (parser.at >= parser.text.len()) break;
        local escape = parser.text[parser.at];
        parser.at += 1;
        if (escape == 'n') out += "\n";
        else if (escape == 't') out += "\t";
        else if (escape == 'r') out += "\r";
        else if (escape == 'b') out += "\b";
        else if (escape == 'f') out += "\f";
        else if (escape == 'u') {
            // The level schema only writes ASCII, so a \u escape is decoded to a
            // placeholder rather than silently producing a wrong code point.
            parser.at += 4;
            out += "?";
        } else out += escape.tochar();
    }
    levelJsonFail(parser, "unterminated string");
}

function levelJsonParseNumber(parser) {
    local start = parser.at;
    while (parser.at < parser.text.len()) {
        local c = parser.text[parser.at];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E')
            parser.at += 1;
        else break;
    }
    local literal = parser.text.slice(start, parser.at);
    if (literal.find(".") != null || literal.find("e") != null || literal.find("E") != null)
        return literal.tofloat();
    return literal.tointeger();
}

function levelJsonParseValue(parser) {
    levelJsonSkip(parser);
    if (parser.at >= parser.text.len()) levelJsonFail(parser, "unexpected end of input");
    local c = parser.text[parser.at];

    if (c == '{') {
        parser.at += 1;
        local table = {};
        levelJsonSkip(parser);
        if (parser.at < parser.text.len() && parser.text[parser.at] == '}') { parser.at += 1; return table; }
        while (true) {
            levelJsonSkip(parser);
            local key = levelJsonParseString(parser);
            levelJsonExpect(parser, ':');
            table[key] <- levelJsonParseValue(parser);
            levelJsonSkip(parser);
            if (parser.at < parser.text.len() && parser.text[parser.at] == ',') { parser.at += 1; continue; }
            levelJsonExpect(parser, '}');
            return table;
        }
    }
    if (c == '[') {
        parser.at += 1;
        local array = [];
        levelJsonSkip(parser);
        if (parser.at < parser.text.len() && parser.text[parser.at] == ']') { parser.at += 1; return array; }
        while (true) {
            array.push(levelJsonParseValue(parser));
            levelJsonSkip(parser);
            if (parser.at < parser.text.len() && parser.text[parser.at] == ',') { parser.at += 1; continue; }
            levelJsonExpect(parser, ']');
            return array;
        }
    }
    if (c == '"') return levelJsonParseString(parser);
    if (parser.text.find("true", parser.at) == parser.at) { parser.at += 4; return true; }
    if (parser.text.find("false", parser.at) == parser.at) { parser.at += 5; return false; }
    if (parser.text.find("null", parser.at) == parser.at) { parser.at += 4; return null; }
    return levelJsonParseNumber(parser);
}

function levelJsonDecode(text) {
    local parser = levelJsonParser(text);
    local value = levelJsonParseValue(parser);
    levelJsonSkip(parser);
    if (parser.at != text.len()) levelJsonFail(parser, "trailing content");
    return value;
}
