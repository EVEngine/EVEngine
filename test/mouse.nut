
function basic() {
    if (eve.Mouse == null) {
        print("can not find Mouse");
        return false;
    }

    local p = eve.Mouse();
    if (p.getName() != "Mouse") {
        print("Mouse name is not right: ");
        print(p.getName());
        return false;
    }
    return true;
}
