#pragma once
#include "common/Export.h"


#include "common/Module.h"
#include "common/Result.h"

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
    /** @brief Begins a nested-counted transaction (`BEGIN` on the outermost enter). */
    [[nodiscard]] eve::Result<void> beginTransaction();
    /** @brief Commits one nesting level (`COMMIT` when the outermost level exits). */
    [[nodiscard]] eve::Result<void> commitTransaction();
    /** @brief Rolls back the whole open transaction and clears nesting. */
    [[nodiscard]] eve::Result<void> rollbackTransaction();
    /** @brief Compatibility-only boolean facade over beginTransaction(). */
    bool             begin();
    /** @brief Compatibility-only boolean facade over commitTransaction(). */
    bool             commit();
    /** @brief Compatibility-only boolean facade over rollbackTransaction(). */
    bool             rollback();
    /** @brief True while at least one beginTransaction() nesting level is open. */
    bool             isInTransaction() const;
    /** @brief Compatibility-only boolean alias of isInTransaction(). */
    bool             inTransaction() const;
    /** @brief Executes a statement; returns the affected row count. */
    int              execute(const std::string& sql);
    /** @brief Runs a SELECT and returns every row as name/value maps. */
    std::vector<Row> query(const std::string& sql);
    /** @brief Runs query() and serializes the rows to a JSON array. */
    std::string      queryJson(const std::string& sql);
    /** @brief Inserts a row map; returns the affected row count. */
    int              insert(const std::string& table, const Row& values);
    /** @brief Insert-or-replace a row map (`INSERT OR REPLACE`); returns affected rows. */
    int              upsert(const std::string& table, const Row& values);
    /** @brief Updates rows matching whereClause; returns the affected row count. */
    int              update(const std::string& table, const Row& values, const std::string& whereClause);
    /** @brief Deletes rows matching whereClause; returns the affected row count. */
    int              remove(const std::string& table, const std::string& whereClause);

    /** @brief Script-friendly insert: JSON must be an object of column/value pairs. */
    int insertJson(const std::string& table, const std::string& json);
    /** @brief Script-friendly insert-or-replace from a JSON object. */
    int upsertJson(const std::string& table, const std::string& json);
    /** @brief Script-friendly update; whereClause is raw SQL. */
    int updateJson(const std::string& table, const std::string& json, const std::string& whereClause);

    /** @brief Updates a single row by primary key value. */
    int              updateByKey(const std::string& table, const Row& values, const std::string& key, const Value& id);
    /** @brief Deletes a single row by primary key value. */
    int              removeByKey(const std::string& table, const std::string& key, const Value& id);
    /** @brief Selects rows where key == id. */
    std::vector<Row> queryByKey(const std::string& table, const std::string& key, const Value& id);
    /**
     * @brief Builds a script-facing Model repository for a table.
     * @ownership Caller deletes.
     * @lifetime Valid until the caller deletes it; must not outlive this Connection.
     */
    Model*           model(std::string table, std::string primaryKey = "id");
    /**
     * @brief Builds a fluent Query builder for a table.
     * @ownership Caller deletes.
     * @lifetime Valid until the caller deletes it; must not outlive this Connection.
     */
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
    int                                  transactionDepth_ = 0;
};

/** @brief Composable filtered/sorted/paged query shared by C++ and scripts. */
class EVENGINE_API_FOUNDATION Query {
public:
    /** @brief Queries Query. */
    Query(Connection* connection, std::string table);

    /**
     * @brief Adds a quoted projection column (default remains SELECT *).
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* select(const std::string& column);
    /**
     * @brief Adds a quoted projection column with AS alias.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* selectAs(const std::string& column, const std::string& alias);
    /**
     * @brief Adds a validated aggregate/expression projection with optional alias.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* selectExpr(const std::string& expression, const std::string& alias = {});
    /**
     * @brief Script helper: JSON array of names, {column,as}, or {expr,as}.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* selectJson(const std::string& columnsJson);
    /**
     * @brief SELECT DISTINCT …
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* distinct(bool enabled = true);

    /**
     * @brief INNER JOIN table ON leftColumn = rightColumn (dotted names allowed).
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* innerJoin(const std::string& table, const std::string& leftColumn, const std::string& rightColumn);
    /**
     * @brief LEFT JOIN table ON leftColumn = rightColumn (dotted names allowed).
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* leftJoin(const std::string& table, const std::string& leftColumn, const std::string& rightColumn);

    /**
     * @brief AND-combined comparison predicate.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* where(const std::string& column, const std::string& op, Value value);
    /**
     * @brief Script AND where; valueJson is one JSON scalar.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereJson(const std::string& column, const std::string& op, const std::string& valueJson);
    /**
     * @brief OR-combined comparison predicate.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* orWhere(const std::string& column, const std::string& op, Value value);
    /**
     * @brief Script OR where; valueJson is one JSON scalar.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* orWhereJson(const std::string& column, const std::string& op, const std::string& valueJson);
    /**
     * @brief AND column IN (…); valuesJson is a JSON array of scalars.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereIn(const std::string& column, const std::string& valuesJson);
    /**
     * @brief AND column NOT IN (…).
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereNotIn(const std::string& column, const std::string& valuesJson);
    /**
     * @brief AND column BETWEEN lo AND hi.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereBetween(const std::string& column, Value low, Value high);
    /**
     * @brief Script BETWEEN; lowJson/highJson are JSON scalars.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereBetweenJson(const std::string& column, const std::string& lowJson, const std::string& highJson);
    /**
     * @brief AND column IS NULL.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereNull(const std::string& column);
    /**
     * @brief AND column IS NOT NULL.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereNotNull(const std::string& column);
    /**
     * @brief AND (p0 OR p1 OR …); predicatesJson is [{column,op,value}, …].
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* whereAnyJson(const std::string& predicatesJson);
    /**
     * @brief OR (p0 OR p1 OR …) against the previous predicate.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* orWhereAnyJson(const std::string& predicatesJson);

    /**
     * @brief Adds a GROUP BY column (dotted names allowed).
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* groupBy(const std::string& column);
    /**
     * @brief Script GROUP BY from a JSON array of column names.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* groupByJson(const std::string& columnsJson);
    /**
     * @brief AND-style HAVING comparison (after GROUP BY).
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* having(const std::string& columnOrExpr, const std::string& op, Value value);
    /**
     * @brief Script HAVING; valueJson is one JSON scalar.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* havingJson(const std::string& columnOrExpr, const std::string& op, const std::string& valueJson);

    /**
     * @brief Order by.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* orderBy(const std::string& column, bool ascending = true);
    /**
     * @brief Limit.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* limit(int count);
    /**
     * @brief Offset.
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* offset(int count);
    /** @brief All. */
    std::vector<Row> all();
    /** @brief All json. */
    std::string      allJson();
    /** @brief First json. */
    std::string      firstJson();
    /** @brief Returns matching row count (or number of groups when GROUP BY is set). */
    int              count();

private:
    struct Predicate {
        bool        combineWithOr = false;
        std::string sql;
    };
    struct Join {
        std::string type;  // INNER / LEFT
        std::string table;
        std::string left;
        std::string right;
    };

    std::string selectSql(bool countOnly, int forcedLimit = -1) const;
    /**
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* addComparison(bool combineWithOr, const std::string& column, const std::string& op, Value value);
    /**
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* addIn(bool negate, const std::string& column, const std::string& valuesJson);
    /**
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* addJoin(std::string type, std::string table, std::string leftColumn, std::string rightColumn);
    /**
     * @ownership Borrowed; returns this for chaining.
     * @lifetime Valid until this Query is destroyed.
     */
    Query* addAnyGroup(bool combineWithOr, const std::string& predicatesJson);

    Connection*              connection_;
    std::string              table_;
    std::vector<std::string> columns_;
    std::vector<Join>        joins_;
    std::vector<Predicate>   predicates_;
    std::vector<std::string> groups_;
    std::vector<std::string> havings_;
    std::vector<std::string> ordering_;
    int                      limit_    = -1;
    int                      offset_   = 0;
    bool                     distinct_ = false;
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
