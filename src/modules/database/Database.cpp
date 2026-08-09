#include "database/Database.h"

#include <Poco/Data/RecordSet.h>
#include <Poco/Data/SQLite/Connector.h>
#include <Poco/Data/Session.h>
#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace eve::database {
namespace {

std::string quoteIdentifier(const std::string& name) {
    if (name.empty()) throw std::invalid_argument("database identifier must not be empty");
    std::string quoted = "\"";
    for (char c : name) {
        if (c == '\0') throw std::invalid_argument("database identifier contains NUL");
        quoted += c == '"' ? "\"\"" : std::string(1, c);
    }
    return quoted + "\"";
}

std::string literal(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return "NULL";
    if (auto p = std::get_if<std::int64_t>(&value)) return std::to_string(*p);
    if (auto p = std::get_if<double>(&value)) return std::to_string(*p);
    if (auto p = std::get_if<bool>(&value)) return *p ? "1" : "0";
    std::string escaped;
    for (char c : std::get<std::string>(value)) escaped += c == '\'' ? "''" : std::string(1, c);
    return "'" + escaped + "'";
}

Value fromDynamic(const Poco::Dynamic::Var& value) {
    if (value.isEmpty()) return std::monostate{};
    if (value.isBoolean()) return value.convert<bool>();
    if (value.isInteger()) return value.convert<Poco::Int64>();
    if (value.isNumeric()) return value.convert<double>();
    return value.convert<std::string>();
}

Row parseObject(const std::string& json) {
    auto object = Poco::JSON::Parser().parse(json).extract<Poco::JSON::Object::Ptr>();
    Row  row;
    for (const auto& name : object->getNames()) row[name] = fromDynamic(object->get(name));
    return row;
}

Poco::Dynamic::Var toDynamic(const Value& value);
Value              parseScalar(const std::string& json) {
    auto wrapper = Poco::JSON::Parser().parse("[" + json + "]").extract<Poco::JSON::Array::Ptr>();
    if (wrapper->size() != 1) throw std::invalid_argument("ORM id must be one JSON scalar");
    auto value = wrapper->get(0);
    if (!value.isEmpty() && !value.isBoolean() && !value.isNumeric() && !value.isString())
        throw std::invalid_argument("ORM id must be a JSON scalar");
    return fromDynamic(value);
}

std::string rowsToJson(const std::vector<Row>& source) {
    Poco::JSON::Array rows;
    for (const auto& row : source) {
        Poco::JSON::Object object;
        for (const auto& [key, value] : row) object.set(key, toDynamic(value));
        rows.add(object);
    }
    std::ostringstream output;
    Poco::JSON::Stringifier::stringify(rows, output);
    return output.str();
}

Poco::Dynamic::Var toDynamic(const Value& value) {
    if (std::holds_alternative<std::monostate>(value)) return {};
    if (auto p = std::get_if<std::int64_t>(&value)) return Poco::Dynamic::Var(Poco::Int64(*p));
    if (auto p = std::get_if<double>(&value)) return Poco::Dynamic::Var(*p);
    if (auto p = std::get_if<bool>(&value)) return Poco::Dynamic::Var(*p);
    return Poco::Dynamic::Var(std::get<std::string>(value));
}

}  // namespace

Connection::Connection(std::string connector, std::string connectionString) {
    Poco::Data::SQLite::Connector::registerConnector();
    session_ = std::make_unique<Poco::Data::Session>(connector, connectionString);
}

Connection::~Connection() { close(); }
bool Connection::isConnected() const { return session_ && session_->isConnected(); }
void Connection::close() {
    if (session_ && session_->isConnected()) session_->close();
}

int Connection::execute(const std::string& sql) {
    if (!isConnected()) throw std::runtime_error("database connection is closed");
    return static_cast<int>((*session_ << sql).execute());
}

std::vector<Row> Connection::query(const std::string& sql) {
    if (!isConnected()) throw std::runtime_error("database connection is closed");
    Poco::Data::Statement statement(*session_);
    statement << sql;
    statement.execute();
    Poco::Data::RecordSet records(statement);
    std::vector<Row>      result;
    for (std::size_t rowIndex = 0; rowIndex < records.rowCount(); ++rowIndex) {
        Row row;
        for (std::size_t column = 0; column < records.columnCount(); ++column)
            row[records.columnName(column)] = fromDynamic(records.value(column, rowIndex));
        result.push_back(std::move(row));
    }
    return result;
}

std::string Connection::queryJson(const std::string& sql) { return rowsToJson(query(sql)); }

int Connection::insert(const std::string& table, const Row& values) {
    if (values.empty()) throw std::invalid_argument("insert values must not be empty");
    std::string columns, data;
    for (const auto& [key, value] : values) {
        if (!columns.empty()) {
            columns += ',';
            data += ',';
        }
        columns += quoteIdentifier(key);
        data += literal(value);
    }
    return execute("INSERT INTO " + quoteIdentifier(table) + " (" + columns + ") VALUES (" + data + ")");
}

int Connection::update(const std::string& table, const Row& values, const std::string& whereClause) {
    if (values.empty()) throw std::invalid_argument("update values must not be empty");
    std::string assignments;
    for (const auto& [key, value] : values) {
        if (!assignments.empty()) assignments += ',';
        assignments += quoteIdentifier(key) + '=' + literal(value);
    }
    return execute("UPDATE " + quoteIdentifier(table) + " SET " + assignments +
                   (whereClause.empty() ? "" : " WHERE " + whereClause));
}

int Connection::remove(const std::string& table, const std::string& whereClause) {
    return execute("DELETE FROM " + quoteIdentifier(table) + (whereClause.empty() ? "" : " WHERE " + whereClause));
}

int Connection::insertJson(const std::string& table, const std::string& json) {
    return insert(table, parseObject(json));
}
int Connection::updateJson(const std::string& table, const std::string& json, const std::string& whereClause) {
    return update(table, parseObject(json), whereClause);
}

int Connection::updateByKey(const std::string& table, const Row& values, const std::string& key, const Value& id) {
    return update(table, values, quoteIdentifier(key) + "=" + literal(id));
}

int Connection::removeByKey(const std::string& table, const std::string& key, const Value& id) {
    return remove(table, quoteIdentifier(key) + "=" + literal(id));
}

std::vector<Row> Connection::queryByKey(const std::string& table, const std::string& key, const Value& id) {
    return query("SELECT * FROM " + quoteIdentifier(table) + " WHERE " + quoteIdentifier(key) + "=" + literal(id) +
                 " LIMIT 1");
}

Model* Connection::model(std::string table, std::string primaryKey) {
    return new Model(this, std::move(table), std::move(primaryKey));
}
Query* Connection::from(std::string table) { return new Query(this, std::move(table)); }

Query::Query(Connection* connection, std::string table) : connection_(connection), table_(std::move(table)) {
    if (!connection_) throw std::invalid_argument("query requires a connection");
}

Query* Query::where(const std::string& column, const std::string& op, Value value) {
    static const std::vector<std::string> allowed{"=", "!=", "<", "<=", ">", ">=", "LIKE"};
    if (std::find(allowed.begin(), allowed.end(), op) == allowed.end())
        throw std::invalid_argument("unsupported query operator: " + op);
    predicates_.push_back(quoteIdentifier(column) + " " + op + " " + literal(value));
    return this;
}

Query* Query::whereJson(const std::string& column, const std::string& op, const std::string& valueJson) {
    return where(column, op, parseScalar(valueJson));
}

Query* Query::orderBy(const std::string& column, bool ascending) {
    ordering_.push_back(quoteIdentifier(column) + (ascending ? " ASC" : " DESC"));
    return this;
}

Query* Query::limit(int count) {
    if (count < 0) throw std::invalid_argument("query limit must not be negative");
    limit_ = count;
    return this;
}

Query* Query::offset(int count) {
    if (count < 0) throw std::invalid_argument("query offset must not be negative");
    offset_ = count;
    return this;
}

std::string Query::selectSql(bool countOnly, int forcedLimit) const {
    std::string sql = countOnly ? "SELECT COUNT(*) AS count FROM " : "SELECT * FROM ";
    sql += quoteIdentifier(table_);
    for (std::size_t i = 0; i < predicates_.size(); ++i) sql += (i == 0 ? " WHERE " : " AND ") + predicates_[i];
    if (!countOnly && !ordering_.empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < ordering_.size(); ++i) sql += (i ? "," : "") + ordering_[i];
    }
    int effectiveLimit = forcedLimit >= 0 ? forcedLimit : limit_;
    if (!countOnly && effectiveLimit >= 0)
        sql += " LIMIT " + std::to_string(effectiveLimit);
    else if (!countOnly && offset_ > 0)
        sql += " LIMIT -1";
    if (!countOnly && offset_ > 0) sql += " OFFSET " + std::to_string(offset_);
    return sql;
}

std::vector<Row> Query::all() { return connection_->query(selectSql(false)); }
std::string      Query::allJson() { return rowsToJson(all()); }
std::string      Query::firstJson() { return rowsToJson(connection_->query(selectSql(false, 1))); }
int              Query::count() {
    auto rows = connection_->query(selectSql(true));
    if (rows.empty()) return 0;
    const Value& value = rows.front().at("count");
    if (auto integer = std::get_if<std::int64_t>(&value)) return static_cast<int>(*integer);
    if (auto number = std::get_if<double>(&value)) return static_cast<int>(*number);
    if (auto text = std::get_if<std::string>(&value)) return std::stoi(*text);
    throw std::runtime_error("database COUNT returned a non-numeric value");
}

Model::Model(Connection* connection, std::string table, std::string primaryKey)
    : connection_(connection), table_(std::move(table)), primaryKey_(std::move(primaryKey)) {
    if (!connection_) throw std::invalid_argument("ORM model requires a connection");
}

int         Model::insertJson(const std::string& json) { return connection_->insertJson(table_, json); }
std::string Model::allJson() { return connection_->queryJson("SELECT * FROM " + quoteIdentifier(table_)); }
std::string Model::findJson(const std::string& idJson) {
    return rowsToJson(connection_->queryByKey(table_, primaryKey_, parseScalar(idJson)));
}
int Model::updateJson(const std::string& idJson, const std::string& json) {
    return connection_->updateByKey(table_, parseObject(json), primaryKey_, parseScalar(idJson));
}
int Model::remove(const std::string& idJson) {
    return connection_->removeByKey(table_, primaryKey_, parseScalar(idJson));
}
Query* Model::query() { return new Query(connection_, table_); }

Module_IMPL(Database, new Database());
Connection* Database::connect(std::string connector, std::string connectionString) {
    return new Connection(std::move(connector), std::move(connectionString));
}
Connection* Database::connectSQLite(std::string path) { return connect("SQLite", std::move(path)); }

void Database::expose(ssq::Table& table) {
    auto module = table.addClass(name, Database::create, false);
    expose(module);
    auto connection = table.addClass<Connection>(
        "DatabaseConnection", std::function<Connection*()>([] { return new Connection("SQLite", ":memory:"); }), true);
    connection.addFunc("isConnected", &Connection::isConnected);
    connection.addFunc("close", &Connection::close);
    connection.addFunc("execute", &Connection::execute);
    connection.addFunc("queryJson", &Connection::queryJson);
    connection.addFunc("insertJson", &Connection::insertJson);
    connection.addFunc("updateJson", &Connection::updateJson);
    connection.addFunc("remove", &Connection::remove);
    connection.addFunc("model", &Connection::model);
    connection.addFunc("from", &Connection::from);

    auto query =
        table.addClass<Query>("DatabaseQuery", std::function<Query*()>([]() -> Query* { return nullptr; }), true);
    query.addFunc("where", &Query::whereJson);
    query.addFunc("orderBy", &Query::orderBy);
    query.addFunc("limit", &Query::limit);
    query.addFunc("offset", &Query::offset);
    query.addFunc("allJson", &Query::allJson);
    query.addFunc("firstJson", &Query::firstJson);
    query.addFunc("count", &Query::count);

    auto model =
        table.addClass<Model>("DatabaseModel", std::function<Model*()>([]() -> Model* { return nullptr; }), true);
    model.addFunc("insertJson", &Model::insertJson);
    model.addFunc("allJson", &Model::allJson);
    model.addFunc("findJson", &Model::findJson);
    model.addFunc("updateJson", &Model::updateJson);
    model.addFunc("remove", &Model::remove);
    model.addFunc("query", &Model::query);
}

void Database::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Database::getName);
    cls.addFunc("connect", &Database::connect);
    cls.addFunc("connectSQLite", &Database::connectSQLite);
}

}  // namespace eve::database
