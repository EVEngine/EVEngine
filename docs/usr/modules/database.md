# 数据库（Database）

**脚本入口：** `eve.Database()`

基于 Poco Data + SQLite 的关系型数据库访问：原始 SQL、JSON 行接口和
轻量 ORM（`Model`）。适合游戏存档、配置表和批量数据导入。

## 基本用法

```squirrel
db <- eve.Database();
local conn = db.connectSQLite("game.db");
conn.execute("CREATE TABLE IF NOT EXISTS actors (id INTEGER, name TEXT, hp REAL)");
conn.insertJson("actors", @"{""id"":1,""name"":""hero"",""hp"":100}");
local rows = conn.queryJson("SELECT * FROM actors ORDER BY id");
local decoded = eve.json.decode(rows);
if (decoded.ok) foreach (row in decoded.value) print(row.id + " " + row.name + "\n");
conn.close();
```

## 目标导向指南

### 保存 / 读取玩家进度

用 `updateJson("actors", "{...}", "id=1")` 局部更新字段，`findJson`/`firstJson`
读取；多行写入用 `begin`/`commit`（或 `upsertJson`）保证原子性。

### 批量查询与分页

链式 `Query` 支持投影、JOIN、AND/OR/`whereAny`、IN/BETWEEN/NULL、GROUP BY/HAVING
与分页：

```squirrel
local q = conn.from("officers");
q.select("officers.name");
q.select("forces.name");
q.select("officers.leadership");
q.innerJoin("forces", "officers.force_id", "forces.id");
q.where("officers.force_id", "=", "\"wei\"");
q.whereAny(@"[
  {""column"":""war"",""op"":"">="",""value"":90},
  {""column"":""leadership"",""op"":"">="",""value"":95}
]");
q.orderBy("officers.leadership", false);
q.limit(20);
local page = q.allJson();
local total = q.count();
```

`where` / `orWhere` / `whereBetween` 的第三个参数是 **JSON 标量文本**
（数字写 `"90"`，字符串写 `"\"wei\""`）。`whereIn` / `selectJson` / `groupByJson` /
`whereAny` 吃 JSON 数组。

聚合示例：

```squirrel
local g = conn.from("cities");
g.select("nation_id");
g.selectExpr("SUM(gold)", "treasury");
g.selectExpr("COUNT(*)", "city_count");
g.groupBy("nation_id");
g.having("COUNT(*)", ">=", "1");
g.orderBy("nation_id", true);
local rows = g.allJson();
```

复杂到构建器表达不了时，仍用 `queryJson("SELECT …")` 逃生舱。

## 绑定与行为说明

### `Database`（模块）

- `getName()`：模块名（"Database"）。
- `connect(connector, connectionString)` / `connectSQLite(path)` → `Connection`。

### `Connection`

- `isConnected()`、`close()`。
- `beginTransaction()` / `commitTransaction()` / `rollbackTransaction()` / `isInTransaction()`：
  嵌套计数事务（`Result`）；`begin`/`commit`/`rollback`/`inTransaction` 为兼容布尔门面。
- `execute(sql)`：任意 DDL/DML。
- `queryJson(sql)`：返回行数组的 JSON 字符串（再用 `eve.json.decode`）。
- `insertJson` / `upsertJson` / `updateJson` / `remove`。
- `model(table, pk)` → `Model`；`from(table)` → `Query`。

### `Query`

- 投影：`select(column)`、`selectExpr(expr, alias)`、`selectJson(array)`、`distinct(bool)`。
- 连接：`innerJoin(table, leftCol, rightCol)`、`leftJoin(...)`（列可用 `table.column`）。
- 过滤：`where` / `orWhere`、`whereIn` / `whereNotIn`、`whereBetween`、
  `whereNull` / `whereNotNull`、`whereAny` / `orWhereAny`。
- 分组：`groupBy` / `groupByJson`、`having`。
- 排序分页：`orderBy(column, ascending)`、`limit`、`offset`。
- 结果：`allJson()` / `firstJson()` / `count()`。

### `Model`

- `insertJson` / `allJson` / `findJson` / `updateJson` / `remove` / `query()` → `Query`。

## 生命周期

- `Connection` 由脚本持有，`close()` 后不可再用；模块不缓存连接。
- SQLite 文件路径走引擎虚拟文件系统（写目录见 `filesystem.setupWriteDirectory`）。
- 热重载脚本时若反复建表，用 `IF NOT EXISTS` 保证幂等。
