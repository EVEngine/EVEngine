#include "database/Database.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

using namespace eve::database;

TEST_CASE("Database.SQLiteCrudOrmAndECSExport") {
    Connection db("SQLite", ":memory:");
    CHECK(db.isConnected());
    db.execute("CREATE TABLE actors (id INTEGER, name TEXT, hp REAL)");

    struct Actor {
        std::int64_t id{};
        std::string  name;
        double       hp{};
    };
    Mapping<Actor> mapping{"actors",
                           {
                               {"id", [](const Actor& a) -> Value { return a.id; },
                                [](Actor& a, const Value& v) { a.id = std::get<std::int64_t>(v); }, "INTEGER", true},
                               {"name", [](const Actor& a) -> Value { return a.name; },
                                [](Actor& a, const Value& v) { a.name = std::get<std::string>(v); }},
                               {"hp", [](const Actor& a) -> Value { return a.hp; },
                                [](Actor& a, const Value& v) { a.hp = std::get<double>(v); }},
                           }};

    Repository<Actor> actors(db, mapping);
    // A real ORM owns schema and typed entity CRUD rather than only serializing a row.
    db.execute("DROP TABLE actors");
    actors.createTable();
    CHECK_EQ(actors.insert(Actor{1, "mage", 80.0}), 1);
    db.insertJson("actors", R"({"id":2,"name":"knight","hp":100})");
    CHECK_EQ(db.updateJson("actors", R"({"hp":95})", "id=2"), 1);

    std::vector<Actor> ecsView{{3, "rogue", 70.0}, {4, "cleric", 90.0}};
    CHECK_EQ(db.exportECS("actors", ecsView, [&mapping](const Actor& actor) { return mapping.toRow(actor); }), 2);
    auto rows = db.query("SELECT id,name,hp FROM actors ORDER BY id");
    CHECK_EQ(rows.size(), 4);
    CHECK_EQ(std::get<std::string>(rows[1]["name"]), "knight");
    CHECK_EQ(db.remove("actors", "id=1"), 1);
    CHECK_EQ(db.query("SELECT id FROM actors").size(), 3);

    Actor loaded;
    CHECK(actors.find(std::int64_t(2), loaded));
    CHECK_EQ(loaded.name, "knight");
    loaded.hp = 88.0;
    CHECK_EQ(actors.update(loaded), 1);
    CHECK_EQ(actors.all().size(), 3);
    CHECK_EQ(actors.remove(std::int64_t(2)), 1);

    std::unique_ptr<Model> model(db.model("actors", "id"));
    model->insertJson(R"({"id":5,"name":"bard","hp":60})");
    CHECK(model->findJson("5").find("bard") != std::string::npos);
    CHECK_EQ(model->updateJson("5", R"({"hp":65})"), 1);
    CHECK_EQ(model->remove("5"), 1);

    db.insertJson("actors", R"({"id":6,"name":"strategist","hp":75})");
    db.insertJson("actors", R"({"id":7,"name":"general","hp":99})");
    std::unique_ptr<Query> ranked(model->query());
    ranked->where("hp", ">=", 70.0)->orderBy("hp", false)->limit(2);
    auto ranking = ranked->all();
    CHECK_EQ(ranking.size(), 2);
    CHECK_EQ(std::get<std::string>(ranking[0]["name"]), "general");
    CHECK_EQ(ranked->count(), 4);
}
