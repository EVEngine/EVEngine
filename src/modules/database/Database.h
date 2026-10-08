#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace Poco::Data {
class Session;
}

namespace eve::database {

class Model;
class Query;

using Value = std::variant<std::monostate, std::int64_t, double, std::string, bool>;
using Row   = std::unordered_map<std::string, Value>;

/** @brief A named field used by the small, explicit ORM mapping layer. */
template <class T>
/** @brief Field public API. */
struct Field {
    std::string                           name;
    /** @brief Value. */
    std::function<Value(const T&)>        read;
    /** @brief Void. */
    std::function<void(T&, const Value&)> write;
    std::string                           sqlType    = "TEXT";
    bool                                  primaryKey = false;
};

template <class T>
/** @brief Mapping public API. */
struct Mapping {
    std::string           table;
    std::vector<Field<T>> fields;

    /** @brief To row. */
    Row toRow(const T& object) const {
        Row row;
        for (const auto& field : fields) row[field.name] = field.read(object);
        return row;
    }

    /** @brief From row. */
    T fromRow(const Row& row) const {
        T object{};
        for (const auto& field : fields) {
            auto it = row.find(field.name);
            if (it != row.end()) field.write(object, it->second);
        }
        return object;
    }
};

/**
 * @brief SQLite connection with a small ORM layer on top of Poco::Data.
 * Tables/columns are quoted identifiers; values are bound as literals.
 */
class EVENGINE_API_FOUNDATION Connection {
public:
    /** @brief Opens a Poco::Data session; connector is e.g. "SQLite". */
    Connection(std::string connector, std::string connectionString);
    /** @brief Connection. */
    ~Connection();

    Connection(const Connection&)            = delete;
    Connection& operator=(const Connection&) = delete;

    /** @brief True while the underlying session is open. */
    bool             isConnected() const;
    /** @brief Closes the session (idempotent). */
    void             close();
    /** @brief Executes a statement; returns the affected row count. */
    int              execute(const std::string& sql);
    /** @brief Runs a SELECT and returns every row as name/value maps. */
    std::vector<Row> query(const std::string& sql);
    /** @brief Runs query() and serializes the rows to a JSON array. */
    std::string      queryJson(const std::string& sql);
    /** @brief Inserts a row map; returns the affected row count. */
    int              insert(const std::string& table, const Row& values);
    /** @brief Updates rows matching whereClause; returns the affected row count. */
    int              update(const std::string& table, const Row& values, const std::string& whereClause);
    /** @brief Deletes rows matching whereClause; returns the affected row count. */
    int              remove(const std::string& table, const std::string& whereClause);

    /** @brief Script-friendly insert: JSON must be an object of column/value pairs. */
    int insertJson(const std::string& table, const std::string& json);
    /** @brief Script-friendly update; whereClause is raw SQL. */
    int updateJson(const std::string& table, const std::string& json, const std::string& whereClause);

    /** @brief Updates a single row by primary key value. */
    int              updateByKey(const std::string& table, const Row& values, const std::string& key, const Value& id);
    /** @brief Deletes a single row by primary key value. */
    int              removeByKey(const std::string& table, const std::string& key, const Value& id);
    /** @brief Selects rows where key == id. */
    std::vector<Row> queryByKey(const std::string& table, const std::string& key, const Value& id);
    /** @brief Builds a script-facing Model repository for a table. */
    Model*           model(std::string table, std::string primaryKey = "id");
    /** @brief Builds a fluent Query builder for a table. */
    Query*           from(std::string table);

    /** @brief ORM helper: persists one mapped object. */
    template <class T>
    /** @brief Saves save. */
    int save(const Mapping<T>& mapping, const T& object) {
        /** @brief Inserts insert. */
        return insert(mapping.table, mapping.toRow(object));
    }

    /** @brief ORM helper: persists a range of mapped entities in one transaction. */
    template <class Range, class Mapper>
    /** @brief Export ecs. */
    int exportECS(const std::string& table, const Range& entities, Mapper mapper) {
        int count = 0;
        /** @brief Execute. */
        execute("BEGIN");
        try {
            for (const auto& entity : entities) count += insert(table, mapper(entity));
            /** @brief Execute. */
            execute("COMMIT");
        } catch (...) {
            try {
                /** @brief Execute. */
                execute("ROLLBACK");
            } catch (...) {
            }
            throw;
        }
        return count;
    }

private:
    std::unique_ptr<Poco::Data::Session> session_;
};

/** @brief Composable filtered/sorted/paged query shared by C++ and scripts. */
class EVENGINE_API_FOUNDATION Query {
public:
    /** @brief Queries Query. */
    Query(Connection* connection, std::string table);
    /** @brief Where. */
    Query*           where(const std::string& column, const std::string& op, Value value);
    /** @brief Where json. */
    Query*           whereJson(const std::string& column, const std::string& op, const std::string& valueJson);
    /** @brief Order by. */
    Query*           orderBy(const std::string& column, bool ascending = true);
    /** @brief Limit. */
    Query*           limit(int count);
    /** @brief Offset. */
    Query*           offset(int count);
    /** @brief All. */
    std::vector<Row> all();
    /** @brief All json. */
    std::string      allJson();
    /** @brief First json. */
    std::string      firstJson();
    /** @brief Returns the number of count. */
    int              count();

private:
    std::string              selectSql(bool countOnly, int forcedLimit = -1) const;
    Connection*              connection_;
    std::string              table_;
    std::vector<std::string> predicates_;
    std::vector<std::string> ordering_;
    int                      limit_  = -1;
    int                      offset_ = 0;
};

/** @brief Typed active-record repository built from a Mapping<T>. */
template <class T>
/** @brief Repository public API. */
class Repository {
public:
    /** @brief Constructs a Repository. */
    Repository(Connection& connection, Mapping<T> mapping) : connection_(connection), mapping_(std::move(mapping)) {}

    /** @brief Creates table. */
    void createTable(bool ifNotExists = true) {
        if (mapping_.fields.empty()) throw std::invalid_argument("ORM mapping has no fields");
        std::string sql = "CREATE TABLE ";
        if (ifNotExists) sql += "IF NOT EXISTS ";
        sql += mapping_.table + " (";
        for (std::size_t i = 0; i < mapping_.fields.size(); ++i) {
            const auto& field = mapping_.fields[i];
            if (i) sql += ',';
            sql += field.name + " " + field.sqlType;
            if (field.primaryKey) sql += " PRIMARY KEY";
        }
        connection_.execute(sql + ")");
    }

    /** @brief Inserts insert. */
    int insert(const T& object) { return connection_.insert(mapping_.table, mapping_.toRow(object)); }

    /** @brief All. */
    std::vector<T> all() {
        std::vector<T> objects;
        for (const auto& row : connection_.query("SELECT * FROM " + mapping_.table))
            objects.push_back(mapping_.fromRow(row));
        return objects;
    }

    /** @brief Finds find. */
    bool find(const Value& id, T& object) {
        auto rows = connection_.queryByKey(mapping_.table, primaryKey().name, id);
        if (rows.empty()) return false;
        object = mapping_.fromRow(rows.front());
        return true;
    }

    /** @brief Updates update. */
    int update(const T& object) {
        const auto& key    = primaryKey();
        Row         values = mapping_.toRow(object);
        Value       id     = values.at(key.name);
        values.erase(key.name);
        return connection_.updateByKey(mapping_.table, values, key.name, id);
    }

    /** @brief Removes remove. */
    int remove(const Value& id) { return connection_.removeByKey(mapping_.table, primaryKey().name, id); }

    /** @brief Select. */
    std::vector<T> select(const std::function<void(Query&)>& configure) {
        /** @brief Queries query. */
        Query query(&connection_, mapping_.table);
        /** @brief Configure. */
        configure(query);
        std::vector<T> objects;
        for (const auto& row : query.all()) objects.push_back(mapping_.fromRow(row));
        return objects;
    }

private:
    const Field<T>& primaryKey() const {
        for (const auto& field : mapping_.fields)
            if (field.primaryKey) return field;
        throw std::logic_error("ORM mapping requires one primary key field");
    }

    Connection& connection_;
    Mapping<T>  mapping_;
};

/** @brief Runtime ORM model exposed to Squirrel. */
class EVENGINE_API_FOUNDATION Model {
public:
    /** @brief Model. */
    Model(Connection* connection, std::string table, std::string primaryKey);
    /** @brief Inserts json. */
    int         insertJson(const std::string& json);
    /** @brief All json. */
    std::string allJson();
    /** @brief Finds json. */
    std::string findJson(const std::string& idJson);
    /** @brief Updates json. */
    int         updateJson(const std::string& idJson, const std::string& json);
    /** @brief Removes remove. */
    int         remove(const std::string& idJson);
    /** @brief Queries query. */
    Query*      query();

private:
    Connection* connection_;
    std::string table_;
    std::string primaryKey_;
};

/**
 * @brief Database module (eve.Database): SQLite connection factory.
 * Script: `db <- eve.Database(); conn <- db.connectSQLite("game.db");`
 */
class Database : public Module {
public:
    Module_REG(Database);
    /** @brief Constructs a Database. */
    Database()           = default;
    /** @brief Releases Database resources. */
    ~Database() override = default;

    /** @brief Opens a connection with an explicit Poco connector name. */
    Connection* connect(std::string connector, std::string connectionString);
    /** @brief Opens a SQLite database file (":memory:" is supported). */
    Connection* connectSQLite(std::string path);
};

}  // namespace eve::database
