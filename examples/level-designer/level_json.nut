// Native JSON uses the common owning Value codec: finite data, strict duplicate
// keys, UTF-8, bounded recursion/element count, and structured diagnostics.
// No script-source evaluation or per-byte VM string concatenation is involved.
function levelJsonEncode(value) {
    local encoded=eve.json.encode(value);
    if(!encoded.ok) throw encoded.status.summary;
    return encoded.value;
}
function levelJsonDecode(text) {
    local decoded=eve.json.decode(text);
    if(!decoded.ok) throw decoded.status.summary;
    return decoded.value;
}
