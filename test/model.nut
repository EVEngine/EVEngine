

class Person {
    name = ""
    age = 0
    race = "human"

    constructor(n, a, r = "human") {
        name = n;
        age = a;
        race = r;
        eve.reg(this);
    }

    function print() {
        ::print(name+"("+race+age+")");
    }
}

class Render {
    photo = null
    x = 0
    y = 0
    card = null

    constructor(p, _x, _y) {
        photo = eve.Image("img/"+p.name+".png")
        card = eve.Image("img/"+p.race+".png")
        x = _x
        y = _y
    }

    function draw() {
        local g = eve.graphic()
        g.draw(card, x, y)
        g.draw(photo, x+50, y+30)
    }
}

eve.model(Person);

function init() {
    local people = [];
    people.append(Person("Jim", 32));
    people.append(Person("Trump", 69));
    people.append(Person("Simi", 12, "elf"));
    people.append(Person("Fujin", 2, "robot"));
    people.append(Person("Kai", 5, "robot"));
    eve.set("people", people);
}

function update() {
    if (eve.isClick()) {
        print("click");
        init();
    }
    local people = eve.get("people");
    for (local i = 0; i < people.len(); i++) {
        people[i].print();
    }
}


function basic() {
    print("basic");
    return true;
}