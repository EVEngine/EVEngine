
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

class Attr {
    name = null;
    type = "int";
}

class Test1 {
    attr1 = Attr;
    attr2 = null;

    constructor(a, b) {
        attr1 = a;
        attr2 = b;
    }

    function test() {
        ::print(attr1);
        ::print(attr2);
    }
}


function testDefClass() {
    local a = Test1(1, 2);
    a.test();
    return true;
}

function getterTest() {
    return true;
}


class Canvas {
    function print(text) {
        ::print(text)
    }
}

class Avatar {

    name = ""
    fps = ""

    </ type = Canvas />
    can = null

    constructor(_name){
        name = _name
        can = Canvas()
    }

    function getCanvas() {
        return can
    }

    function update(dt) {
        fps = dt+" ms"
    }

    function draw() {
        can.print(fps)
    }

}

