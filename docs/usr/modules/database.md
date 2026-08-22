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
foreach (row in rows) print(row.id + " " + row.name + "\n");
conn.close();
```

## 目标导向指南

### 保存 / 读取玩家进度

用 `updateJson("actors", "{...}", "id=1")` 局部更新字段，`findJson`/`firstJson`
读取；存档表按版本号管理，配合 `data` 模块做 JSON 编码。

### 批量查询与分页

`conn.from("actors").where("hp>?", 50).orderBy("hp", "desc").limit(10).offset(0)`
的链式 Query 返回 `allJson()` / `firstJson()` / `count()`，避免手拼 SQL。

## API 快查

### `Database`（模块）

- `getName()`：模块名（"Database"）。
- `connect(path)` / `connectSQLite(path)` → `Connection`。

### `Connection`

- `isConnected()`、`close()`。
- `execute(sql)`：任意 DDL/DML。
- `queryJson(sql)`：返回行数组（JSON 对象）。
- `insertJson(table, obj)` / `updateJson(table, obj, whereSql)` / `remove(table, whereSql)`。
- `model(table, pk)` → `Model`；`from(table)` → `Query`。

### `Query`

- `where(json)`、`orderBy(column, dir)`、`limit(n)`、`offset(n)`。
- `allJson()` / `firstJson()` / `count()`。

### `Model`

- `insertJson(obj)`、`allJson()`、`findJson(id)`、`updateJson(obj, id)`、
  `remove(id)`、`query()` → `Query`。

## 生命周期

- `Connection` 由脚本持有，`close()` 后不可再用；模块不缓存连接。
- SQLite 文件路径走引擎虚拟文件系统（写目录见 `filesystem.setupWriteDirectory`）。
- 热重载脚本时若反复建表，用 `IF NOT EXISTS` 保证幂等。
