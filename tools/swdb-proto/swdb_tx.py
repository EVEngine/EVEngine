"""swdb_tx.py — Semantic World Database P2 参考实现（事务 / 多 Agent 冲突检测切片）

对照 EVEngine 文档 docs/dev/SemanticWorldDatabase架构设计.md：
  §6（事务化修改与 MVCC）、§7（多 Agent 语义冲突检测）、§13.8（多 Agent 工程协调）。

P2 验收点（文档 §11）：
  - 两个 Agent 并发修改可复现成功合并、拒绝和重试；
  - 运行时写入参与检测（游戏系统直写不能成为绕过冲突检测的隐藏写者）；
  - 注入 owner 失败时无部分发布（原子性）。

实现要点：
  - 事务 = tx_id + actor + base_revision + read_set（字段版本 CAS）+ write_set
    + 范围谓词（phantom 检测）+ 命令列表。
  - 提交：dry-run 验证全部通过后原子应用，revision 一次性递增；
    失败返回结构化 Conflict（字段、当前版本、期望版本、重试建议）。
  - undo 是带当前前置条件的新事务（文档 §6：「undo 是带当前前置条件的新事务，
    不能把整个世界倒回旧快照并覆盖其他 Agent 的修改」）。
"""

from __future__ import annotations

import itertools
import threading
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List, Optional, Tuple

from swdb import (
    PersistentId, SchemaError, TombstoneError, UnknownEntityError, World,
)


# ---------------------------------------------------------------------------
# §6 前置条件与冲突
# ---------------------------------------------------------------------------

@dataclass(frozen=True)
class FieldVersion:
    """CAS 前置条件：期望 (pid, field) 处于指定版本。"""

    pid: PersistentId
    field: str
    version: int  # 字段最后修改的 revision；实体不存在时由调用方另行表达


class Conflict(Exception):
    """结构化冲突：Agent 据此重新规划，而不是盲目重试。"""

    def __init__(self, kind: str, detail: Dict[str, Any],
                 retry_hint: Optional[str] = None,
                 actor: Optional[str] = None) -> None:
        super().__init__(f"conflict[{kind}]: {detail}")
        self.kind = kind          # version-mismatch | phantom | unique-violation
                                  # | dangling-reference | owner-rejected | entity-missing
        self.detail = detail
        self.retry_hint = retry_hint
        self.actor = actor

    def to_json(self) -> Dict[str, Any]:
        return {
            "kind": self.kind,
            "detail": self.detail,
            "retryHint": self.retry_hint,
            "actor": self.actor,
        }


# ---------------------------------------------------------------------------
# §7 领域约束
# ---------------------------------------------------------------------------

class ConstraintRegistry:
    """领域约束注册表：唯一性、引用完整性、owner 验证（可注入失败）。"""

    def __init__(self) -> None:
        self._uniques: List[Tuple[str, str, str]] = []   # (domain, type_id, key_field)
        self._references: List[Tuple[str, str, str]] = []  # (field, target_domain) 按字段名
        self._owner_validators: Dict[str, Callable[[PersistentId, str, Any], None]] = {}

    def register_unique(self, domain: str, type_id: str, key_field: str) -> None:
        """区域内唯一约束：例如「一个 dungeon 只有一个出生点」。"""
        self._uniques.append((domain, type_id, key_field))

    def register_reference(self, field_id: str, target_domain: str) -> None:
        """强引用约束：字段值必须是 target_domain 域内存活实体。"""
        self._references.append((field_id, target_domain))

    def register_owner_validator(self, owner: str,
                                 fn: Callable[[PersistentId, str, Any], None]) -> None:
        """owner 安全点验证：fn 抛错则整个事务被拒绝（用于注入 owner 失败）。"""
        self._owner_validators[owner] = fn

    # -- 校验（dry-run 阶段调用） -----------------------------------------
    def check_unique(self, world: World, pid: PersistentId, type_id: str,
                     key_field: str, value: Any) -> None:
        for domain, ctype, ckey in self._uniques:
            if ctype != type_id or ckey != key_field:
                continue
            snap = world.snapshot()
            for other, otype, data in snap.iter_entities():
                if other == pid:
                    continue
                if otype == type_id and data.get(key_field) == value:
                    raise Conflict(
                        "unique-violation",
                        {"domain": domain, "type": type_id,
                         "field": key_field, "value": value,
                         "existing": str(other)},
                        retry_hint="修改 key 值或选择其它区域 / 槽位",
                    )

    def check_references(self, world: World, pid: PersistentId, data: Dict[str, Any]) -> None:
        for ref_field, target_domain in self._references:
            if ref_field not in data:
                continue
            ref = data[ref_field]
            if not isinstance(ref, str):
                continue
            ref_pid = _parse_pid(ref)
            if ref_pid.domain != target_domain:
                raise Conflict(
                    "dangling-reference",
                    {"field": ref_field, "value": ref,
                     "expectedDomain": target_domain},
                    retry_hint=f"引用必须指向 {target_domain} 域",
                )
            row = world.snapshot().get(ref_pid)
            if row is None:
                raise Conflict(
                    "dangling-reference",
                    {"field": ref_field, "value": ref, "missing": ref},
                    retry_hint="目标已删除：更新引用或先恢复目标",
                )

    def check_owner(self, world: World, pid: PersistentId, field_id: str,
                    value: Any) -> None:
        for owner, fn in self._owner_validators.items():
            fn(pid, field_id, value)  # 抛错 → Conflict(owner-rejected)


# ---------------------------------------------------------------------------
# 审计日志
# ---------------------------------------------------------------------------

@dataclass
class AuditEntry:
    tx_id: str
    actor: str
    base_revision: int
    result: str            # committed | conflict | owner-rejected | aborted
    commands: List[str]
    diff: Dict[str, Any]
    at_revision: int
    conflict: Optional[Dict[str, Any]] = None


class AuditLog:
    def __init__(self) -> None:
        self.entries: List[AuditEntry] = []
        self._lock = threading.Lock()

    def append(self, entry: AuditEntry) -> None:
        with self._lock:
            self.entries.append(entry)

    def tail(self, n: int = 20) -> List[AuditEntry]:
        with self._lock:
            return list(self.entries[-n:])


# ---------------------------------------------------------------------------
# 事务
# ---------------------------------------------------------------------------

_TX_SEQ = itertools.count(1)


class Transaction:
    """单事务工作单元：read → write/create/delete → dry_run → commit/abort。

    read_set 记录参与决策的字段版本；write_set 记录期望的旧版本与新值；
    范围谓词记录 base_revision 快照（phantom 检测）。
    """

    def __init__(self, world: World, constraints: ConstraintRegistry,
                 manager: "TransactionManager", actor: str,
                 base_revision: Optional[int] = None) -> None:
        self.world = world
        self.constraints = constraints
        self.manager = manager
        self.actor = actor
        self.tx_id = f"tx-{next(_TX_SEQ)}"
        self.base_revision = world.revision if base_revision is None else base_revision
        self.read_set: Dict[Tuple[PersistentId, str], int] = {}
        self.write_set: Dict[Tuple[PersistentId, str], Tuple[int, Any]] = {}
        self.old_values: Dict[Tuple[PersistentId, str], Any] = {}
        self.pending_create: Dict[PersistentId, Tuple[str, Dict[str, Any]]] = {}
        self.pending_delete: set[PersistentId] = set()
        self.deleted_snapshot: Dict[PersistentId, Tuple[str, Dict[str, Any]]] = {}
        self.range_predicates: List[Tuple[Tuple[int, int], Tuple[int, int], int]] = []
        self.commands: List[str] = []
        self._aborted = False

    # -- 读 ----------------------------------------------------------------
    def read(self, pid: PersistentId, field: str) -> Tuple[int, Any]:
        """读取字段并记录 read_set（CAS 依据：读到时的字段版本）。"""
        snap = self.world.snapshot()
        row = snap.get(pid)
        if row is None:
            raise UnknownEntityError(f"{pid} 不存在（read {field}）")
        _type, data = row
        version = self.world.field_version(pid, field) or 0
        self.read_set[(pid, field)] = version
        self.commands.append(f"read {pid}.{field} @v{version}")
        return version, data.get(field)

    def read_range(self, within: Tuple[float, float, float]) -> None:
        """记录范围谓词依赖：commit 时若 base 后有新对象落入该范围 → phantom。"""
        cx, cy, r = within
        lo = (int((cx - r) // 8.0), int((cy - r) // 8.0))
        hi = (int((cx + r) // 8.0), int((cy + r) // 8.0))
        self.range_predicates.append((lo, hi, self.base_revision))
        self.commands.append(f"read_range center=({cx},{cy}) r={r}")

    # -- 写 ----------------------------------------------------------------
    def write(self, pid: PersistentId, field: str, value: Any,
              expected_version: Optional[int] = None) -> None:
        """写入（dry-run 不落盘）：自动记录读到时的字段版本与旧值。

        expected_version：MCP 调用方声明的前置条件（文档 §6 FieldVersion）。
        提供时跳过自动重读，用声明版本做 CAS。
        """
        if expected_version is not None:
            row = self.world.snapshot().get(pid)
            old = row[1].get(field) if row is not None else None
            self.read_set[(pid, field)] = expected_version
            version = expected_version
        else:
            version, old = self.read(pid, field)
        self.write_set[(pid, field)] = (version, value)
        self.old_values[(pid, field)] = old
        self.commands.append(f"write {pid}.{field} = {value!r} (expect v{version})")

    def create(self, type_id: str, domain: str = "world",
               **fields: Any) -> PersistentId:
        """创建（提交时才落盘）：预占稳定 ID（烧掉也不复用）。"""
        pid = self.world.allocate_pending_id(domain)
        self.pending_create[pid] = (type_id, fields)
        self.commands.append(f"create {pid} ({type_id})")
        return pid

    def delete(self, pid: PersistentId) -> None:
        """删除：先完整快照原值，供条件 undo 重建。"""
        snap = self.world.snapshot()
        row = snap.get(pid)
        if row is None:
            raise UnknownEntityError(f"{pid} 不存在（delete）")
        self.deleted_snapshot[pid] = row
        self.pending_delete.add(pid)
        self.commands.append(f"delete {pid}")

    # -- 验证与提交 --------------------------------------------------------
    def _check_versions(self) -> None:
        """CAS：每个 write 目标的当前字段版本必须等于读取时的版本。"""
        for (pid, field), (expect_v, _new) in self.write_set.items():
            current = self.world.field_version(pid, field) or 0
            if current != expect_v:
                raise Conflict(
                    "version-mismatch",
                    {"pid": str(pid), "field": field,
                     "currentVersion": current, "expectedVersion": expect_v},
                    retry_hint="重读字段最新版本后重试（先重新查询再提交）",
                    actor=self.actor,
                )
        for pid in self.pending_delete:
            try:
                self.world._require_alive(pid)
            except TombstoneError as e:
                raise Conflict(
                    "entity-missing",
                    {"pid": str(pid), "detail": str(e)},
                    retry_hint="目标已被其他事务删除",
                    actor=self.actor,
                )

    def _check_phantom(self) -> None:
        """范围谓词 phantom 检测：base 之后落入范围内的新对象使旧事务失效。"""
        if not self.range_predicates:
            return
        snap = self.world.snapshot()
        for lo, hi, base_rev in self.range_predicates:
            for pid, type_id, data in snap.iter_entities():
                # 只对 base 后新创建的实体做 phantom 判定：借用字段版本推断创建时间
                if self.world.field_version(pid, "position") is None:
                    continue
                created_after_base = (
                    self.world.field_version(pid, "position")
                    is not None and self.world.field_version(pid, "position") > base_rev)
                if not created_after_base:
                    continue
                pos = data.get("position")
                if pos and (lo[0] <= pos[0] // 8.0 <= hi[0]
                            and lo[1] <= pos[1] // 8.0 <= hi[1]):
                    raise Conflict(
                        "phantom",
                        {"range": {"lo": lo, "hi": hi},
                         "newEntity": str(pid), "type": type_id},
                        retry_hint="范围内新增了对象：重新查询并带上新的谓词版本",
                        actor=self.actor,
                    )

    def _check_constraints(self) -> None:
        """领域约束：唯一性 + 引用完整性 + owner 验证（dry-run 阶段全跑完）。"""
        for pid, (type_id, fields) in self.pending_create.items():
            for cfield, cvalue in fields.items():
                self.constraints.check_unique(
                    self.world, pid, type_id, cfield, cvalue)
            self.constraints.check_references(self.world, pid, fields)
            self.constraints.check_owner(self.world, pid, "__create__", type_id)
        for (pid, field), (_v, value) in self.write_set.items():
            self.constraints.check_owner(self.world, pid, field, value)

    def dry_run(self) -> Dict[str, Any]:
        """只验证不落盘：返回影响范围（diff / 命令数）。"""
        if self._aborted:
            raise RuntimeError(f"{self.tx_id} 已 abort")
        self._check_versions()
        self._check_phantom()
        self._check_constraints()
        return {
            "commands": len(self.commands),
            "writes": len(self.write_set),
            "creates": len(self.pending_create),
            "deletes": len(self.pending_delete),
        }

    def commit(self) -> Dict[str, Any]:
        """验证全部通过后原子应用；任何失败 → Conflict，世界零变化。"""
        if self._aborted:
            raise RuntimeError(f"{self.tx_id} 已 abort")
        plan = self.dry_run()
        diff = self._apply()
        entry = AuditEntry(
            tx_id=self.tx_id, actor=self.actor,
            base_revision=self.base_revision, result="committed",
            commands=list(self.commands), diff=diff,
            at_revision=self.world.revision,
        )
        self.manager.audit.append(entry)
        return {"txId": self.tx_id, "ok": True, **plan, "diff": diff,
                "revision": self.world.revision}

    def _apply(self) -> Dict[str, Any]:
        """原子应用：creates → writes → deletes，最后一次性递增 revision。"""
        created: List[Tuple[PersistentId, str, Dict[str, Any]]] = []
        old_values: Dict[Tuple[PersistentId, str], Any] = {}
        for pid, (type_id, fields) in self.pending_create.items():
            comp = self.world.schema.get(type_id)
            data = comp.defaults()
            for k, v in fields.items():
                data[k] = comp.field(k).coerce(v)
            self.world._entities[pid] = data
            self.world._types[pid] = type_id
            created.append((pid, type_id, data))
        for (pid, field), (_v, value) in self.write_set.items():
            old = self.world._entities[pid][field]
            old_values[(pid, field)] = old
            self.world._entities[pid][field] = value
        for pid in self.pending_delete:
            del self.world._entities[pid]
            del self.world._types[pid]
        self.world.revision += 1
        # 字段版本统一更新到新 revision（一次发布，中间态不可见）
        for pid, _t, data in created:
            for k in data:
                self.world._field_versions[(pid, k)] = self.world.revision
        for (pid, field) in self.write_set:
            self.world._field_versions[(pid, field)] = self.world.revision
        for pid in self.pending_delete:
            self.world._tombstones[pid] = self.world.revision
            for key in [k for k in self.world._field_versions if k[0] == pid]:
                del self.world._field_versions[key]
        return {
            "created": [str(p) for p, _, _ in created],
            "updated": [f"{p}.{f}" for (p, f) in old_values],
            "deleted": [str(p) for p in self.pending_delete],
        }

    def abort(self, reason: Optional[str] = None) -> None:
        self._aborted = True
        self.manager.audit.append(AuditEntry(
            tx_id=self.tx_id, actor=self.actor,
            base_revision=self.base_revision, result="aborted",
            commands=list(self.commands), diff={},
            at_revision=self.world.revision,
            conflict={"reason": reason} if reason else None,
        ))

    # -- 条件 undo ---------------------------------------------------------
    def undo(self) -> "Transaction":
        """生成带当前前置条件的新事务（文档 §6），不是回滚整个世界。

        逆向 writes 写回事务**应用前**的旧值（old_values），前置条件 = 当前字段版本，
        因此 undo 本身也可能与其他并发写入冲突——这是文档要求的语义。
        """
        undo_tx = self.manager.begin(self.actor)
        for (pid, field), (_v, _new) in self.write_set.items():
            if self.world.snapshot().get(pid) is not None:
                undo_tx.write(pid, field, self.old_values[(pid, field)])
        # 逆向 creates：若仍存活则删除
        for pid in self.pending_create:
            if self.world.snapshot().get(pid) is not None:
                undo_tx.delete(pid)
        # 逆向 deletes：按删除前快照重建
        for pid, (type_id, data) in self.deleted_snapshot.items():
            if self.world.snapshot().get(pid) is None:
                undo_tx.pending_create[pid] = (type_id, dict(data))
                undo_tx.commands.append(f"restore {pid} ({type_id})")
        return undo_tx


# ---------------------------------------------------------------------------
# 事务管理器
# ---------------------------------------------------------------------------

class TransactionManager:
    """事务入口：begin(actor) → tx.read/write/create/delete → commit。

    与引擎侧对应物：运行时写入（World.set_field 直写）也递增字段版本，
    因此事务提交时 CAS 自然失败——隐藏写者无法绕过冲突检测。
    """

    def __init__(self, world: World) -> None:
        self.world = world
        self.constraints = ConstraintRegistry()
        self.audit = AuditLog()

    def begin(self, actor: str,
              base_revision: Optional[int] = None) -> Transaction:
        return Transaction(self.world, self.constraints, self, actor,
                           base_revision)


# ---------------------------------------------------------------------------
# MCP 契约（对齐文档 §6 commit() 语义示意）
# ---------------------------------------------------------------------------

def mcp_eve_world_commit(manager: TransactionManager, params: Dict[str, Any]) -> Dict[str, Any]:
    """eve_world_commit：把结构化修改意图提交为事务。

    参数：
      { "actor": "agent-A",
        "baseRevision": 42,
        "readSet": [ {"pid":"dungeon:1","field":"locked","version":7} ],
        "commands": [
            {"op":"set", "pid":"dungeon:1", "field":"locked", "value": true},
            {"op":"create", "type":"Chest", "domain":"dungeon", "fields":{"gold":10}},
            {"op":"delete", "pid":"dungeon:5"}
        ],
        "preconditions": [ {"kind":"field-version", "pid":"dungeon:1",
                            "field":"locked", "version":7} ] }
    返回：committed → {ok, txId, revision, diff}；
          冲突 → {ok:false, conflict:{kind, detail, retryHint}}。
    """
    tx = manager.begin(params.get("actor", "mcp"))
    tx.base_revision = params.get("baseRevision", manager.world.revision)
    read_versions = {
        (_parse_pid(r["pid"]), r["field"]): r["version"]
        for r in params.get("readSet", [])
    }
    for cmd in params.get("commands", []):
        op = cmd["op"]
        if op == "set":
            pid = _parse_pid(cmd["pid"])
            exp = read_versions.get((pid, cmd["field"]))
            tx.write(pid, cmd["field"], cmd["value"], expected_version=exp)
        elif op == "create":
            tx.create(cmd.get("type"), cmd.get("domain", "world"),
                      **cmd.get("fields", {}))
        elif op == "delete":
            tx.delete(_parse_pid(cmd["pid"]))
        else:
            return {"ok": False, "error": f"unknown-op:{op}"}
    try:
        result = tx.commit()
        return {"ok": True, **result}
    except Conflict as c:
        tx.manager.audit.append(AuditEntry(
            tx_id=tx.tx_id, actor=tx.actor, base_revision=tx.base_revision,
            result="conflict", commands=list(tx.commands), diff={},
            at_revision=manager.world.revision, conflict=c.to_json(),
        ))
        return {"ok": False, "conflict": c.to_json()}


# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def _parse_pid(s: str) -> PersistentId:
    domain, _, seq = s.rpartition(":")
    return PersistentId(domain, int(seq))
