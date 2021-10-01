
function exportClass() {
    if (eve.Window == null) return false;
    return true;
}

function get() {
    local a = A();
    local b = B();
    a.setString("print");
    b.setA(a);
    return b;
}

function refTest() {
    local a = get();
    a.print();
    return true;
}