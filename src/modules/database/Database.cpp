#include "database/Database.h"

#include "common/Diagnostic.h"
#include "common/SquirrelBinding.h"

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
#include <cctype>
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

/** @brief Quotes `table.column` or a bare column as SQL identifiers. */
std::string qualifyColumn(const std::string& name) {
    if (name.empty()) throw std::invalid_argument("database column must not be empty");
    const auto dot = name.find('.');
    if (dot == std::string::npos) return quoteIdentifier(name);
    if (dot == 0 || dot + 1 >= name.size() || name.find('.', dot + 1) != std::string::npos)
        throw std::invalid_argument("database column must be name or table.name");
    return quoteIdentifier(name.substr(0, dot)) + "." + quoteIdentifier(name.substr(dot + 1));
}

bool isSimpleAlias(const std::string& alias) {
    if (alias.empty()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(alias[0])) || alias[0] == '_')) return false;
    for (char c : alias) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
    }
    return true;
}

/** @brief Allows COUNT(*) / COUNT|SUM|AVG|MIN|MAX("col") / same with table.col. */
std::string validateSelectExpression(const std::string& expression) {
    static const std::string kCountStar = "COUNT(*)";
    std::string              trimmed;
    trimmed.reserve(expression.size());
    for (char c : expression) {
        if (!std::isspace(static_cast<unsigned char>(c))) trimmed.push_back(c);
    }
    if (trimmed == kCountStar) return kCountStar;

    auto parseAgg = [&](const char* fn) -> std::string {
        const std::string prefix = std::string(fn) + "(";
        if (trimmed.size() < prefix.size() + 2 || trimmed.compare(0, prefix.size(), prefix) != 0 ||
            trimmed.back() != ')')
            return {};
        const std::string inner = trimmed.substr(prefix.size(), trimmed.size() - prefix.size() - 1);
        if (inner == "*") {
            if (std::string(fn) != "COUNT") return {};
            return std::string(fn) + "(*)";
        }
        return std::string(fn) + "(" + qualifyColumn(inner) + ")";
    };

    if (auto expr = parseAgg("COUNT"); !expr.empty()) return expr;
    if (auto expr = parseAgg("SUM"); !expr.empty()) return expr;
    if (auto expr = parseAgg("AVG"); !expr.empty()) return expr;
    if (auto expr = parseAgg("MIN"); !expr.empty()) return expr;
    if (auto expr = parseAgg("MAX"); !expr.empty()) return expr;
    throw std::invalid_argument("unsupported select expression: " + expression);
}

std::string validateComparisonOp(const std::string& op) {
    static const std::vector<std::string> allowed{"=", "!=", "<", "<=", ">", ">=", "LIKE"};
    if (std::find(allowed.begin(), allowed.end(), op) == allowed.end())
        throw std::invalid_argument("unsupported query operator: " + op);
    return op;
}

std::string havingTarget(const std::string& columnOrExpr) {
    if (columnOrExpr.find('(') != std::string::npos) return validateSelectExpression(columnOrExpr);
    return qualifyColumn(columnOrExpr);
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

std::vector<Value> parseScalarArray(const std::string& json) {
    auto array = Poco::JSON::Parser().parse(json).extract<Poco::JSON::Array::Ptr>();
    if (!array) throw std::invalid_argument("expected a JSON array of scalars");
    std::vector<Value> values;
    values.reserve(array->size());
    for (std::size_t i = 0; i < array->size(); ++i) {
        auto value = array->get(i);
        if (!value.isEmpty() && !value.isBoolean() && !value.isNumeric() && !value.isString())
            throw std::invalid_argument("IN list entries must be JSON scalars");
        values.push_back(fromDynamic(value));
    }
    if (values.empty()) throw std::invalid_argument("IN list must not be empty");
    return values;
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
    if (session_ && session_->isConnected()) {
        if (transactionDepth_ > 0) {
            try {
                execute("ROLLBACK");
            } catch (...) {
            }
            transactionDepth_ = 0;
        }
        session_->close();
    }
}

eve::Result<void> Connection::beginTransaction() {
    if (!isConnected()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "database connection is closed", "database.beginTransaction"));
    }
    if (transactionDepth_ == 0) execute("BEGIN");
    ++transactionDepth_;
    return eve::Result<void>::success();
}

eve::Result<void> Connection::commitTransaction() {
    if (!isConnected()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "database connection is closed", "database.commitTransaction"));
    }
    if (transactionDepth_ <= 0) {
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::PreconditionViolation, "no open database transaction", "database.commitTransaction"));
    }
    --transactionDepth_;
    if (transactionDepth_ == 0) execute("COMMIT");
    return eve::Result<void>::success();
}

eve::Result<void> Connection::rollbackTransaction() {
    if (!isConnected()) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "database connection is closed",
                                                                 "database.rollbackTransaction"));
    }
    if (transactionDepth_ <= 0) {
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::PreconditionViolation,
                                                                 "no open database transaction",
                                                                 "database.rollbackTransaction"));
    }
    transactionDepth_ = 0;
    execute("ROLLBACK");
    return eve::Result<void>::success();
}

bool Connection::begin() { return beginTransaction().ok(); }

bool Connection::commit() { return commitTransaction().ok(); }

bool Connection::rollback() { return rollbackTransaction().ok(); }

bool Connection::isInTransaction() const { return transactionDepth_ > 0; }

bool Connection::inTransaction() const { return isInTransaction(); }

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

int Connection::upsert(const std::string& table, const Row& values) {
    if (values.empty()) throw std::invalid_argument("upsert values must not be empty");
    std::string columns, data;
    for (const auto& [key, value] : values) {
        if (!columns.empty()) {
            columns += ',';
            data += ',';
        }
        columns += quoteIdentifier(key);
        data += literal(value);
    }
    return execute("INSERT OR REPLACE INTO " + quoteIdentifier(table) + " (" + columns + ") VALUES (" + data + ")");
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
int Connection::upsertJson(const std::string& table, const std::string& json) {
    return upsert(table, parseObject(json));
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

Query* Query::select(const std::string& column) {
    columns_.push_back(qualifyColumn(column));
    return this;
}

Query* Query::selectAs(const std::string& column, const std::string& alias) {
    if (!isSimpleAlias(alias)) throw std::invalid_argument("select alias must be a simple identifier");
    columns_.push_back(qualifyColumn(column) + " AS " + quoteIdentifier(alias));
    return this;
}

Query* Query::selectExpr(const std::string& expression, const std::string& alias) {
    std::string piece = validateSelectExpression(expression);
    if (!alias.empty()) {
        if (!isSimpleAlias(alias)) throw std::invalid_argument("select alias must be a simple identifier");
        piece += " AS " + quoteIdentifier(alias);
    }
    columns_.push_back(std::move(piece));
    return this;
}

Query* Query::selectJson(const std::string& columnsJson) {
    auto array = Poco::JSON::Parser().parse(columnsJson).extract<Poco::JSON::Array::Ptr>();
    if (!array || array->empty()) throw std::invalid_argument("selectJson requires a non-empty JSON array");
    for (std::size_t i = 0; i < array->size(); ++i) {
        auto entry = array->get(i);
        if (entry.isString()) {
            select(entry.convert<std::string>());
            continue;
        }
        auto object = entry.extract<Poco::JSON::Object::Ptr>();
        if (!object) throw std::invalid_argument("selectJson objects require column or expr");
        const std::string alias = object->has("as") ? object->getValue<std::string>("as") : std::string{};
        if (object->has("expr")) {
            selectExpr(object->getValue<std::string>("expr"), alias);
            continue;
        }
        if (object->has("column")) {
            if (alias.empty())
                select(object->getValue<std::string>("column"));
            else
                selectAs(object->getValue<std::string>("column"), alias);
            continue;
        }
        throw std::invalid_argument("selectJson objects require column or expr");
    }
    return this;
}

Query* Query::distinct(bool enabled) {
    distinct_ = enabled;
    return this;
}

Query* Query::addJoin(std::string type, std::string table, std::string leftColumn, std::string rightColumn) {
    if (table.empty()) throw std::invalid_argument("join table must not be empty");
    joins_.push_back(Join{std::move(type), std::move(table), qualifyColumn(leftColumn), qualifyColumn(rightColumn)});
    return this;
}

Query* Query::innerJoin(const std::string& table, const std::string& leftColumn, const std::string& rightColumn) {
    return addJoin("INNER", table, leftColumn, rightColumn);
}

Query* Query::leftJoin(const std::string& table, const std::string& leftColumn, const std::string& rightColumn) {
    return addJoin("LEFT", table, leftColumn, rightColumn);
}

Query* Query::addComparison(bool combineWithOr, const std::string& column, const std::string& op, Value value) {
    predicates_.push_back(
        Predicate{combineWithOr, qualifyColumn(column) + " " + validateComparisonOp(op) + " " + literal(value)});
    return this;
}

Query* Query::where(const std::string& column, const std::string& op, Value value) {
    return addComparison(false, column, op, std::move(value));
}

Query* Query::whereJson(const std::string& column, const std::string& op, const std::string& valueJson) {
    return where(column, op, parseScalar(valueJson));
}

Query* Query::orWhere(const std::string& column, const std::string& op, Value value) {
    return addComparison(true, column, op, std::move(value));
}

Query* Query::orWhereJson(const std::string& column, const std::string& op, const std::string& valueJson) {
    return orWhere(column, op, parseScalar(valueJson));
}

Query* Query::addIn(bool negate, const std::string& column, const std::string& valuesJson) {
    const auto         values = parseScalarArray(valuesJson);
    std::ostringstream list;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) list << ',';
        list << literal(values[i]);
    }
    predicates_.push_back(
        Predicate{false, qualifyColumn(column) + (negate ? " NOT IN (" : " IN (") + list.str() + ")"});
    return this;
}

Query* Query::whereIn(const std::string& column, const std::string& valuesJson) {
    return addIn(false, column, valuesJson);
}

Query* Query::whereNotIn(const std::string& column, const std::string& valuesJson) {
    return addIn(true, column, valuesJson);
}

Query* Query::whereBetween(const std::string& column, Value low, Value high) {
    predicates_.push_back(
        Predicate{false, qualifyColumn(column) + " BETWEEN " + literal(low) + " AND " + literal(high)});
    return this;
}

Query* Query::whereBetweenJson(const std::string& column, const std::string& lowJson, const std::string& highJson) {
    return whereBetween(column, parseScalar(lowJson), parseScalar(highJson));
}

Query* Query::whereNull(const std::string& column) {
    predicates_.push_back(Predicate{false, qualifyColumn(column) + " IS NULL"});
    return this;
}

Query* Query::whereNotNull(const std::string& column) {
    predicates_.push_back(Predicate{false, qualifyColumn(column) + " IS NOT NULL"});
    return this;
}

Query* Query::addAnyGroup(bool combineWithOr, const std::string& predicatesJson) {
    auto array = Poco::JSON::Parser().parse(predicatesJson).extract<Poco::JSON::Array::Ptr>();
    if (!array || array->empty()) throw std::invalid_argument("whereAny requires a non-empty JSON array");
    std::ostringstream group;
    group << '(';
    for (std::size_t i = 0; i < array->size(); ++i) {
        auto object = array->getObject(static_cast<unsigned>(i));
        if (!object || !object->has("column") || !object->has("op") || !object->has("value"))
            throw std::invalid_argument("whereAny entries need column, op, and value");
        if (i) group << " OR ";
        group << qualifyColumn(object->getValue<std::string>("column")) << " "
              << validateComparisonOp(object->getValue<std::string>("op")) << " "
              << literal(fromDynamic(object->get("value")));
    }
    group << ')';
    predicates_.push_back(Predicate{combineWithOr, group.str()});
    return this;
}

Query* Query::whereAnyJson(const std::string& predicatesJson) { return addAnyGroup(false, predicatesJson); }

Query* Query::orWhereAnyJson(const std::string& predicatesJson) { return addAnyGroup(true, predicatesJson); }

Query* Query::groupBy(const std::string& column) {
    groups_.push_back(qualifyColumn(column));
    return this;
}

Query* Query::groupByJson(const std::string& columnsJson) {
    auto array = Poco::JSON::Parser().parse(columnsJson).extract<Poco::JSON::Array::Ptr>();
    if (!array || array->empty()) throw std::invalid_argument("groupByJson requires a non-empty JSON array");
    for (std::size_t i = 0; i < array->size(); ++i) {
        auto entry = array->get(i);
        if (!entry.isString()) throw std::invalid_argument("groupByJson entries must be strings");
        groupBy(entry.convert<std::string>());
    }
    return this;
}

Query* Query::having(const std::string& columnOrExpr, const std::string& op, Value value) {
    havings_.push_back(havingTarget(columnOrExpr) + " " + validateComparisonOp(op) + " " + literal(value));
    return this;
}

Query* Query::havingJson(const std::string& columnOrExpr, const std::string& op, const std::string& valueJson) {
    return having(columnOrExpr, op, parseScalar(valueJson));
}

Query* Query::orderBy(const std::string& column, bool ascending) {
    const std::string target =
        column.find('(') != std::string::npos ? validateSelectExpression(column) : qualifyColumn(column);
    ordering_.push_back(target + (ascending ? " ASC" : " DESC"));
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
    if (!havings_.empty() && groups_.empty())
        throw std::invalid_argument("HAVING requires at least one GROUP BY column");

    std::string from = quoteIdentifier(table_);
    for (const auto& join : joins_) {
        from += " " + join.type + " JOIN " + quoteIdentifier(join.table) + " ON " + join.left + " = " + join.right;
    }

    auto appendPredicates = [&](std::string& sql) {
        for (std::size_t i = 0; i < predicates_.size(); ++i) {
            const auto& predicate = predicates_[i];
            if (i == 0)
                sql += " WHERE ";
            else
                sql += predicate.combineWithOr ? " OR " : " AND ";
            sql += predicate.sql;
        }
    };

    if (countOnly) {
        if (!groups_.empty()) {
            std::string inner = "SELECT 1 FROM " + from;
            appendPredicates(inner);
            inner += " GROUP BY ";
            for (std::size_t i = 0; i < groups_.size(); ++i) inner += (i ? "," : "") + groups_[i];
            if (!havings_.empty()) {
                inner += " HAVING ";
                for (std::size_t i = 0; i < havings_.size(); ++i) inner += (i ? " AND " : "") + havings_[i];
            }
            return "SELECT COUNT(*) AS count FROM (" + inner + ")";
        }
        std::string sql = "SELECT COUNT(*) AS count FROM " + from;
        appendPredicates(sql);
        return sql;
    }

    std::string sql = "SELECT ";
    if (distinct_) sql += "DISTINCT ";
    if (columns_.empty())
        sql += "*";
    else {
        for (std::size_t i = 0; i < columns_.size(); ++i) sql += (i ? "," : "") + columns_[i];
    }
    sql += " FROM " + from;
    appendPredicates(sql);
    if (!groups_.empty()) {
        sql += " GROUP BY ";
        for (std::size_t i = 0; i < groups_.size(); ++i) sql += (i ? "," : "") + groups_[i];
    }
    if (!havings_.empty()) {
        sql += " HAVING ";
        for (std::size_t i = 0; i < havings_.size(); ++i) sql += (i ? " AND " : "") + havings_[i];
    }
    if (!ordering_.empty()) {
        sql += " ORDER BY ";
        for (std::size_t i = 0; i < ordering_.size(); ++i) sql += (i ? "," : "") + ordering_[i];
    }
    int effectiveLimit = forcedLimit >= 0 ? forcedLimit : limit_;
    if (effectiveLimit >= 0)
        sql += " LIMIT " + std::to_string(effectiveLimit);
    else if (offset_ > 0)
        sql += " LIMIT -1";
    if (offset_ > 0) sql += " OFFSET " + std::to_string(offset_);
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
    connection.addFunc("beginTransaction", [](HSQUIRRELVM vm, Connection* self) -> ssq::Table {
        if (!self) {
            return eve::script::projectResult(
                vm, eve::Result<void>::failure(eve::Diagnostic::error(
                        eve::DiagnosticCode::InvalidArgument, "null DatabaseConnection", "database.beginTransaction")));
        }
        return eve::script::projectResult(vm, self->beginTransaction());
    });
    connection.addFunc("commitTransaction", [](HSQUIRRELVM vm, Connection* self) -> ssq::Table {
        if (!self) {
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument, "null DatabaseConnection",
                                                      "database.commitTransaction")));
        }
        return eve::script::projectResult(vm, self->commitTransaction());
    });
    connection.addFunc("rollbackTransaction", [](HSQUIRRELVM vm, Connection* self) -> ssq::Table {
        if (!self) {
            return eve::script::projectResult(vm, eve::Result<void>::failure(eve::Diagnostic::error(
                                                      eve::DiagnosticCode::InvalidArgument, "null DatabaseConnection",
                                                      "database.rollbackTransaction")));
        }
        return eve::script::projectResult(vm, self->rollbackTransaction());
    });
    connection.addFunc("begin", &Connection::begin);
    connection.addFunc("commit", &Connection::commit);
    connection.addFunc("rollback", &Connection::rollback);
    connection.addFunc("isInTransaction", &Connection::isInTransaction);
    connection.addFunc("inTransaction", &Connection::inTransaction);
    connection.addFunc("execute", &Connection::execute);
    connection.addFunc("queryJson", &Connection::queryJson);
    connection.addFunc("insertJson", &Connection::insertJson);
    connection.addFunc("upsertJson", &Connection::upsertJson);
    connection.addFunc("updateJson", &Connection::updateJson);
    connection.addFunc("remove", &Connection::remove);
    connection.addFunc("model", &Connection::model);
    connection.addFunc("from", &Connection::from);

    auto query =
        table.addClass<Query>("DatabaseQuery", std::function<Query*()>([]() -> Query* { return nullptr; }), true);
    query.addFunc("select", &Query::select);
    query.addFunc("selectAs", &Query::selectAs);
    query.addFunc("selectExpr", &Query::selectExpr);
    query.addFunc("selectJson", &Query::selectJson);
    query.addFunc("distinct", &Query::distinct);
    query.addFunc("innerJoin", &Query::innerJoin);
    query.addFunc("leftJoin", &Query::leftJoin);
    query.addFunc("where", &Query::whereJson);
    query.addFunc("orWhere", &Query::orWhereJson);
    query.addFunc("whereIn", &Query::whereIn);
    query.addFunc("whereNotIn", &Query::whereNotIn);
    query.addFunc("whereBetween", &Query::whereBetweenJson);
    query.addFunc("whereNull", &Query::whereNull);
    query.addFunc("whereNotNull", &Query::whereNotNull);
    query.addFunc("whereAny", &Query::whereAnyJson);
    query.addFunc("orWhereAny", &Query::orWhereAnyJson);
    query.addFunc("groupBy", &Query::groupBy);
    query.addFunc("groupByJson", &Query::groupByJson);
    query.addFunc("having", &Query::havingJson);
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
