
class Person {
    name = ""
    age = 0
    race = "human"
    level = 0

    constructor(n, a, r = "human") {
        name = n;
        age = a;
        race = r;
        eve.reg(this);
    }

    function print() {
        ::print(name+"("+race+age+") = "+level);
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
    print("basic - modified");

    return true;
}