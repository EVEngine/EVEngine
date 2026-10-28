"""swdb.py — Semantic World Database P1 参考实现（确定性世界查询切片）

对照 EVEngine 文档 docs/dev/SemanticWorldDatabase架构设计.md 的 §2（权威状态与分层）、
§3（稳定 Entity ID）、§4（Schema 与关系图）、§5（混合索引）、§9（运行时查询契约），
把 P1「确定性世界查询」的语义做成可独立运行、可测试的参考实现。

P1 验收点（来自该文档 §11）：
  - 同版本查询与权威扫描一致；
  - 删除、移动、索引落后和裁剪后降级行为可验证；
  - 本阶段无需向量后端；向量缺席时结构化查询仍可用。

本文件只依赖 Python 标准库，可在无 Vulkan / 无引擎的环境中运行与验证。
移植到 C++/ECS 的接口草案见 README.md。P2（事务 / 多 Agent 冲突检测）见 swdb_tx.py。
"""

from __future__ import annotations

import copy
import json
import math
import threading
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, Iterator, List, Optional, Tuple


# ---------------------------------------------------------------------------
# §4 Schema：类型 / 字段契约
# ---------------------------------------------------------------------------

class FieldType(Enum):
    INT = "int"
    FLOAT = "float"
    BOOL = "bool"
    STRING = "string"
    VEC3 = "vec3"  # [x, y, z]


@dataclass(frozen=True)
class FieldDef:
    """字段定义。field_id 稳定，重命名保留；类型改变需要显式迁移（原型仅校验）。"""

    field_id: str
    ftype: FieldType
    default: Any = None
    readonly: bool = False      # 运行时权威字段，Agent 不可直接写
    derived: bool = False       # 派生字段，禁止当作独立状态写入
    owner: str = "runtime"      # 状态 owner（模块 / 领域名）

    def coerce(self, value: Any) -> Any:
        if self.ftype is FieldType.VEC3:
            v = list(value)
            if len(v) != 3 or not all(isinstance(x, (int, float)) for x in v):
                raise SchemaError(f"field {self.field_id}: vec3 需要 3 个数值")
            return v
        if self.ftype is FieldType.INT and not isinstance(value, int):
            raise SchemaError(f"field {self.field_id}: 需要 int")
        if self.ftype is FieldType.FLOAT and not isinstance(value, (int, float)):
            raise SchemaError(f"field {self.field_id}: 需要 float")
        if self.ftype is FieldType.BOOL and not isinstance(value, bool):
            raise SchemaError(f"field {self.field_id}: 需要 bool")
        if self.ftype is FieldType.STRING and not isinstance(value, str):
            raise SchemaError(f"field {self.field_id}: 需要 string")
        return value


@dataclass(frozen=True)
class ComponentDef:
    """组件类型定义。type_id 是稳定组件类型 ID。"""

    type_id: str
    fields: Tuple[FieldDef, ...]
    schema_version: int = 1
    migrate: Optional[Callable[[Dict[str, Any], int], Dict[str, Any]]] = None

    def field(self, field_id: str) -> FieldDef:
        for fd in self.fields:
            if fd.field_id == field_id:
                return fd
        raise SchemaError(f"组件 {self.type_id} 无字段 {field_id}")

    def defaults(self) -> Dict[str, Any]:
        return {fd.field_id: fd.default for fd in self.fields}


class SchemaError(Exception):
    pass


class SchemaRegistry:
    """Schema 注册表：单一来源，注册后组件定义可枚举、可查询。"""

    def __init__(self) -> None:
        self._components: Dict[str, ComponentDef] = {}
        self._revision: int = 0

    def register(self, comp: ComponentDef) -> None:
        if comp.type_id in self._components:
            raise SchemaError(f"组件类型 {comp.type_id} 重复注册")
        seen: set[str] = set()
        for fd in comp.fields:
            if fd.field_id in seen:
                raise SchemaError(f"组件 {comp.type_id} 字段 {fd.field_id} 重复")
            seen.add(fd.field_id)
        self._components[comp.type_id] = comp
        self._revision += 1

    def get(self, type_id: str) -> ComponentDef:
        try:
            return self._components[type_id]
        except KeyError:
            raise SchemaError(f"未注册组件类型 {type_id}")

    def types(self) -> List[str]:
        return list(self._components)

    @property
    def revision(self) -> int:
        return self._revision


# ---------------------------------------------------------------------------
# §3 稳定 Entity ID
# ---------------------------------------------------------------------------

class PersistentId:
    """稳定身份：domain:seq。ID 不复用；删除留下 tombstone，复用槽位永不诞生。

    对齐文档 §3：「一个持久 ID 在同一世界中最多映射到一个活对象；创建检查碰撞；
    销毁留下有版本的 tombstone，过期查询不能解析到复用槽位的新实体。」
    """

    __slots__ = ("domain", "seq")

    def __init__(self, domain: str, seq: int) -> None:
        self.domain = domain
        self.seq = seq

    def __str__(self) -> str:
        return f"{self.domain}:{self.seq}"

    def __hash__(self) -> int:
        return hash((self.domain, self.seq))

    def __eq__(self, other: object) -> bool:
        return isinstance(other, PersistentId) and (
            self.domain == other.domain and self.seq == other.seq
        )

    def to_json(self) -> str:
        return str(self)


class IdAllocator:
    def __init__(self, domain: str) -> None:
        self.domain = domain
        self._next = 1

    def allocate(self) -> PersistentId:
        pid = PersistentId(self.domain, self._next)
        self._next += 1
        return pid


# ---------------------------------------------------------------------------
# §2 权威状态
# ---------------------------------------------------------------------------

class World:
    """权威运行时状态。revision 随每次写入递增；查询必须基于只读快照。

    对齐文档 §2：「ECS 保存权威运行时状态……投影只能读版本化事件或一致快照，
    不得扫描正在并发修改的裸 ECS 指针。」

    字段级版本（P2 依赖）：每个实体字段记录最后修改的 revision，供事务 CAS 使用；
    文档 §6 前置条件示例 `FieldVersion(id, fieldId, 7)`。
    """

    def __init__(self, schema: SchemaRegistry, tick: int = 0) -> None:
        self.schema = schema
        self.tick = tick
        self.revision = 0
        self._allocators: Dict[str, IdAllocator] = {}
        self._entities: Dict[PersistentId, Dict[str, Any]] = {}
        self._types: Dict[PersistentId, str] = {}
        self._tombstones: Dict[PersistentId, int] = {}  # id -> 删除时的 revision
        self._field_versions: Dict[Tuple[PersistentId, str], int] = {}
        self._lock = threading.RLock()

    # -- 身份 -------------------------------------------------------------
    def _alloc(self, domain: str) -> PersistentId:
        a = self._allocators.get(domain)
        if a is None:
            a = IdAllocator(domain)
            self._allocators[domain] = a
        pid = a.allocate()
        if pid in self._entities or pid in self._tombstones:
            raise RuntimeError(f"身份碰撞：{pid} 已存在")  # 文档 §3：创建检查碰撞
        return pid

    def allocate_pending_id(self, domain: str) -> PersistentId:
        """预分配稳定 ID（P2 事务 create 用）：ID 由世界负责分配且永不复用。

        对齐文档 §3：身份分配是世界职责；事务在提交前预占身份，冲突失败时
        已预占的 ID 直接烧掉（不复用），符合「ID 永不复用」的不变量。
        """
        with self._lock:
            return self._alloc(domain)

    # -- 权威写入 ----------------------------------------------------------
    def create(self, type_id: str, domain: str = "world",
               **fields: Any) -> PersistentId:
        comp = self.schema.get(type_id)
        with self._lock:
            pid = self._alloc(domain)
            data = comp.defaults()
            for k, v in fields.items():
                fd = comp.field(k)
                if fd.readonly or fd.derived:
                    raise SchemaError(f"字段 {k} 是 {'只读' if fd.readonly else '派生'}，不可由写入者直接设置")
                data[k] = fd.coerce(v)
            self._entities[pid] = data
            self._types[pid] = type_id
            self.revision += 1
            for k in data:
                self._field_versions[(pid, k)] = self.revision
            return pid

    def set_field(self, pid: PersistentId, field_id: str, value: Any) -> int:
        """权威写入（owner / 系统路径）。Agent 不应绕过事务直接调用。

        运行时写入同样产生字段版本（文档 §6：「游戏系统的直接写入必须产生字段
        版本或使相关编辑事务失效，不能成为绕过冲突检测的隐藏写者」）。
        """
        with self._lock:
            self._require_alive(pid)
            comp = self.schema.get(self._types[pid])
            fd = comp.field(field_id)
            if fd.readonly or fd.derived:
                raise SchemaError(f"字段 {field_id} 为 {'只读' if fd.readonly else '派生'}，拒绝写入")
            self._entities[pid][field_id] = fd.coerce(value)
            self.revision += 1
            self._field_versions[(pid, field_id)] = self.revision
            return self.revision

    def delete(self, pid: PersistentId) -> int:
        with self._lock:
            self._require_alive(pid)
            del self._entities[pid]
            del self._types[pid]
            self.revision += 1
            self._tombstones[pid] = self.revision
            # 实体销毁后，其字段版本随 tombstone 失效
            for key in [k for k in self._field_versions if k[0] == pid]:
                del self._field_versions[key]
            return self.revision

    def advance_tick(self) -> int:
        with self._lock:
            self.tick += 1
            self.revision += 1
            return self.tick

    # -- 只读访问 ----------------------------------------------------------
    def _require_alive(self, pid: PersistentId) -> None:
        if pid not in self._entities:
            if pid in self._tombstones:
                raise TombstoneError(
                    f"{pid} 已被销毁（tombstone @r{self._tombstones[pid]}）",
                    self._tombstones[pid])
            raise UnknownEntityError(f"{pid} 不存在")

    def field_version(self, pid: PersistentId, field_id: str) -> Optional[int]:
        """P2 依赖：读取字段版本（CAS 前置条件）。"""
        return self._field_versions.get((pid, field_id))

    def snapshot(self) -> "WorldSnapshot":
        """返回绑定当前 revision 的只读快照（隔离拷贝视图）。

        实体数据为深拷贝：快照持有的是独立副本，后续权威写入与快照互不可见；
        对快照数据的任何修改也不影响世界（文档 §2 一致只读快照语义）。
        """
        with self._lock:
            return WorldSnapshot(
                world=self, revision=self.revision, tick=self.tick,
                entities={pid: copy.deepcopy(data)
                          for pid, data in self._entities.items()},
                types=dict(self._types),
            )

    def type_of(self, pid: PersistentId) -> str:
        return self._types[pid]

    def tombstone_revision(self, pid: PersistentId) -> Optional[int]:
        return self._tombstones.get(pid)


class WorldSnapshot:
    """一致只读快照。同一 revision 内多次查询结果必须一致。"""

    def __init__(self, world: World, revision: int, tick: int,
                 entities: Dict[PersistentId, Dict[str, Any]],
                 types: Dict[PersistentId, str]) -> None:
        self.world = world
        self.revision = revision
        self.tick = tick
        self._entities = entities
        self._types = types

    def iter_entities(self) -> Iterator[Tuple[PersistentId, str, Dict[str, Any]]]:
        for pid, data in self._entities.items():
            yield pid, self._types[pid], data

    def get(self, pid: PersistentId) -> Optional[Tuple[str, Dict[str, Any]]]:
        data = self._entities.get(pid)
        if data is None:
            return None
        return self._types[pid], data

    def count(self) -> int:
        return len(self._entities)


class TombstoneError(Exception):
    def __init__(self, msg: str, tomb: Optional[int] = None) -> None:
        super().__init__(msg)
        self.tomb = tomb


class UnknownEntityError(Exception):
    pass


# ---------------------------------------------------------------------------
# §5 投影索引（属性 / 空间 / 向量）与索引水位
# ---------------------------------------------------------------------------

@dataclass
class IndexWatermark:
    """索引水位：用于强一致查询的版本核对。"""

    index: str
    revision: int
    available: bool
    lag: bool = False


class AttributeIndex:
    """属性投影：确定性筛选。可声明可索引字段，防止写放大。"""

    def __init__(self, world: World, indexable: Tuple[str, ...]) -> None:
        self.world = world
        self.indexable = set(indexable)
        self._map: Dict[str, Dict[Any, List[PersistentId]]] = {}
        self.revision = -1  # 落后标记
        self.available = True
        self.rebuild()

    def rebuild(self) -> None:
        snap = self.world.snapshot()
        self._map = {}
        for pid, type_id, data in snap.iter_entities():
            for key in self.indexable:
                if key in data:
                    self._map.setdefault(key, {}).setdefault(
                        _norm(data[key]), []).append(pid)
        self.revision = snap.revision

    def mark_lag(self) -> None:
        self.revision = self.world.revision - 1  # 模拟落后

    def lookup(self, key: str, value: Any) -> List[PersistentId]:
        bucket = self._map.get(key, {}).get(_norm(value))
        return list(bucket or [])


class SpatialIndex:
    """空间投影（AABB 宽相候选）。原型用简化的格网桶。"""

    def __init__(self, world: World, pos_field: str = "position",
                 cell: float = 10.0) -> None:
        self.world = world
        self.pos_field = pos_field
        self.cell = cell
        self._grid: Dict[Tuple[int, int], List[PersistentId]] = {}
        self.revision = -1
        self.available = True
        self.rebuild()

    def rebuild(self) -> None:
        snap = self.world.snapshot()
        self._grid = {}
        for pid, _type_id, data in snap.iter_entities():
            pos = data.get(self.pos_field)
            if isinstance(pos, (list, tuple)) and len(pos) >= 2:
                self._grid.setdefault(self._bucket(pos[0], pos[1]), []).append(pid)
        self.revision = snap.revision

    def mark_lag(self) -> None:
        self.revision = self.world.revision - 1

    def _bucket(self, x: float, y: float) -> Tuple[int, int]:
        return (math.floor(x / self.cell), math.floor(y / self.cell))

    def range_query(self, cx: float, cy: float, radius: float) -> List[PersistentId]:
        lo = self._bucket(cx - radius, cy - radius)
        hi = self._bucket(cx + radius, cy + radius)
        out: List[PersistentId] = []
        for bx in range(lo[0], hi[0] + 1):
            for by in range(lo[1], hi[1] + 1):
                out.extend(self._grid.get((bx, by), ()))
        return out


class VectorIndex:
    """语义投影：可选 provider，可缺席、可重建。缺席时结构化查询不受影响。"""

    def __init__(self, world: World, provider: Optional[Any] = None) -> None:
        self.world = world
        self.provider = provider
        self.revision = -1
        self.available = provider is not None

    def similar(self, description: str, k: int) -> List[PersistentId]:
        if not self.available or self.provider is None:
            raise IndexUnavailable("向量索引不可用：provider 未配置（可裁剪）")
        return self.provider.search(description, k)


class IndexUnavailable(Exception):
    pass


class QueryStaleTokenError(Exception):
    """分页 token 绑定的快照 revision 已过期：查询必须基于一致快照。"""

    def __init__(self, token_revision: int, current_revision: int) -> None:
        super().__init__(
            f"分页 token 绑定 r{token_revision}，当前世界 r{current_revision}："
            f"token 已过期，需重新查询")
        self.token_revision = token_revision
        self.current_revision = current_revision


class IndexSet:
    """世界查询用的投影集合：统一水位检查与重建入口。"""

    def __init__(self, attr: AttributeIndex, spatial: SpatialIndex,
                 vector: Optional[VectorIndex] = None) -> None:
        self.attr = attr
        self.spatial = spatial
        self.vector = vector

    def watermarks(self, snapshot_revision: Optional[int] = None
                   ) -> List[IndexWatermark]:
        """返回索引水位；传入快照 revision 时计算 lag 标志（披露用）。"""
        def _lag(index_rev: int, available: bool) -> bool:
            return (available and snapshot_revision is not None
                    and index_rev < snapshot_revision)

        wm = [
            IndexWatermark(
                "attribute", self.attr.revision, self.attr.available,
                lag=_lag(self.attr.revision, self.attr.available)),
            IndexWatermark(
                "spatial", self.spatial.revision, self.spatial.available,
                lag=_lag(self.spatial.revision, self.spatial.available)),
        ]
        if self.vector is not None:
            wm.append(IndexWatermark(
                "vector", self.vector.revision, self.vector.available,
                lag=_lag(self.vector.revision, self.vector.available)))
        return wm

    def strong_consistent(self, snapshot_revision: int,
                          required: Optional[set[str]] = None
                          ) -> Optional[IndexWatermark]:
        """强一致检查：只检查本查询实际依赖的索引集合。

        对齐文档 §5/§10：查询按参与索引决定一致性；高频位置更新只更新空间投影，
        属性索引不因位置移动而失效。
        """
        if required is None:
            required = {"attribute", "spatial"}
        for wm in self.watermarks(snapshot_revision):
            if wm.index == "vector":
                # 向量投影弱一致（文档 §5）：只检查可用性，版本落后由水位披露
                if "vector" in required and not wm.available:
                    return wm
                continue
            if wm.index not in required:
                continue
            if not wm.available or wm.revision < snapshot_revision:
                return wm
        return None


# ---------------------------------------------------------------------------
# §9 运行时查询契约（查询 AST + 规划器 + 预算 + 分页）
# ---------------------------------------------------------------------------

@dataclass
class QuerySpec:
    type: Optional[str] = None
    within: Optional[Tuple[float, float, float]] = None  # (cx, cy, radius)
    where: Dict[str, Any] = field(default_factory=dict)
    similarTo: Optional[str] = None
    limit: int = 100
    consistency: str = "strong"  # strong | eventual
    pageToken: Optional[str] = None
    budgetNodes: int = 10_000


@dataclass
class QueryResult:
    ids: List[str]
    revision: int
    tick: int
    truncated: bool
    budgetUsed: int
    indexWatermarks: List[Dict[str, Any]]
    pageToken: Optional[str]
    reasons: List[str]


class QueryPlanner:
    """把 QuerySpec 编译成执行计划：属性/空间起点 + 后过滤，向量仅作候选辅助。

    对齐文档 §5：「根据选择率选择属性或空间起点，再进行关系过滤；不能固定所有
    查询都先向量 top-k」；§9：「把查询结果用于修改时，必须把相关谓词及字段加入
    事务读集」（P2 语义，见 swdb_tx.py）。
    """

    def __init__(self, world: World, indexes: IndexSet) -> None:
        self.world = world
        self.indexes = indexes

    def execute(self, spec: QuerySpec) -> QueryResult:
        snap = self.world.snapshot()

        # 分页 token 绑定快照 revision：陈旧 token 必须拒绝（不是静默翻新页）
        cursor = 0
        if spec.pageToken:
            try:
                tag, token_rev, token_cursor = spec.pageToken.split(":", 2)
                if tag != "rev" or int(token_rev) != snap.revision:
                    raise QueryStaleTokenError(
                        int(token_rev), snap.revision)
                cursor = int(token_cursor)
            except ValueError:
                raise QueryStaleTokenError(-1, snap.revision)

        # 强一致水位检查（仅检查本查询依赖的索引）
        required: set[str] = set()
        if spec.where:
            required.add("attribute")
        if spec.within:
            required.add("spatial")
        if spec.similarTo:
            required.add("vector")
        if spec.consistency == "strong":
            stale = self.indexes.strong_consistent(snap.revision, required)
            if stale is not None:
                return self._stalled_result(snap, stale)

        budget = spec.budgetNodes
        seen: set[PersistentId] = set()
        reasons: List[str] = []

        # 起点：属性索引优先（确定性筛选）
        candidates: List[PersistentId] = []
        for key, value in spec.where.items():
            hits = self.indexes.attr.lookup(key, value)
            budget -= 1
            if not candidates:
                candidates = hits
                reasons.append(f"起点：属性 {key}={value} ({len(hits)} 候选)")
            else:
                candidates = [pid for pid in candidates if pid in set(hits)]
                reasons.append(f"交集：属性 {key}={value}")

        # 语义投影候选（弱一致起点；向量缺席时由水位检查先行拒绝）
        if spec.similarTo and self.indexes.vector is not None \
                and self.indexes.vector.available:
            try:
                raw = self.indexes.vector.similar(spec.similarTo, k=spec.limit)
                vec_cands = [
                    c if isinstance(c, PersistentId)
                    else PersistentId(*_parse_pid(c))
                    for c in raw
                ]
                if not candidates:
                    candidates = vec_cands
                    reasons.append(f"起点：向量相似 top-k ({len(vec_cands)})")
                else:
                    keep = set(vec_cands)
                    candidates = [pid for pid in candidates if pid in keep]
                    reasons.append("交集：向量相似候选")
            except IndexUnavailable:
                pass

        # 空间起点（若属性起点为空或未给出）
        if spec.within and not candidates:
            cx, cy, radius = spec.within
            spatial = self.indexes.spatial.range_query(cx, cy, radius)
            budget -= len(spatial)
            candidates = spatial
            reasons.append(f"起点：空间半径 {radius} ({len(spatial)} 候选)")

        # 无任何谓词起点（如 type-only 查询）→ 全量权威扫描起点。
        # 注意：带谓词但 0 命中的查询不回退全量（保持空集语义，不做无用扫描）。
        if (not candidates and not spec.where and not spec.within
                and not spec.similarTo):
            candidates = [pid for pid, _t, _d in snap.iter_entities()]
            budget -= len(candidates)
            reasons.append(f"起点：全量权威扫描 ({len(candidates)})")

        # 类型过滤（在权威快照上做精确过滤）
        if spec.type:
            candidates = [
                pid for pid in candidates
                if snap.get(pid) is not None and snap.get(pid)[0] == spec.type
            ]
            reasons.append(f"类型过滤：{spec.type}")

        # 权威快照后过滤（防止索引与真值漂移；空间候选需精确距离）
        out: List[PersistentId] = []
        for pid in candidates:
            row = snap.get(pid)
            if row is None:
                continue  # 索引滞后于快照：跳过（降级可见性）
            _type_id, data = row
            budget -= 1
            if spec.within:
                cx, cy, radius = spec.within
                pos = data.get("position")
                if not pos or dist(pos, (cx, cy)) > radius:
                    continue
            ok = True
            for key, value in spec.where.items():
                if key not in data or _norm(data[key]) != _norm(value):
                    ok = False
                    break
            if ok:
                out.append(pid)
            if budget <= 0:
                break

        truncated = budget <= 0

        # 分页：token 编码快照 revision（rev:<rev>:<cursor>），陈旧 token 在入口拒绝
        page_token = None
        page = out[cursor:cursor + spec.limit]
        if len(page) == spec.limit and cursor + spec.limit < len(out):
            page_token = f"rev:{snap.revision}:{cursor + spec.limit}"

        return QueryResult(
            ids=[str(pid) for pid in page],
            revision=snap.revision,
            tick=snap.tick,
            truncated=truncated,
            budgetUsed=spec.budgetNodes - max(budget, 0),
            indexWatermarks=[asdict_wm(w)
                             for w in self.indexes.watermarks(snap.revision)],
            pageToken=page_token,
            reasons=reasons,
        )

    def _stalled_result(self, snap: WorldSnapshot,
                        stale: IndexWatermark) -> QueryResult:
        """强一致查询遇到落后索引：返回 stalled 状态，不返回陈旧结果。"""
        return QueryResult(
            ids=[], revision=snap.revision, tick=snap.tick,
            truncated=False, budgetUsed=0,
            indexWatermarks=[asdict_wm(w)
                             for w in self.indexes.watermarks(snap.revision)],
            pageToken=None,
            reasons=[f"stalled: 索引 {stale.index} 水位 r{stale.revision} < 快照 r{snap.revision}"],
        )


# ---------------------------------------------------------------------------
# 工具函数
# ---------------------------------------------------------------------------

def _norm(v: Any) -> Any:
    if isinstance(v, (list, tuple)):
        return tuple(v)
    return v


def dist(pos: Any, center: Tuple[float, float]) -> float:
    return math.hypot(pos[0] - center[0], pos[1] - center[1])


def asdict_wm(wm: IndexWatermark) -> Dict[str, Any]:
    return {
        "index": wm.index,
        "revision": wm.revision,
        "available": wm.available,
        "lag": wm.lag,
    }


# ---------------------------------------------------------------------------
# MCP 工具接入契约（对齐文档 §9 接口语义示意）
# ---------------------------------------------------------------------------

def mcp_eve_world_query(planner: QueryPlanner, params: Dict[str, Any]) -> Dict[str, Any]:
    """eve_world_query：把 JSON-RPC 参数映射到 QuerySpec 并执行。

    参数（与文档 §9 的 query() 对齐）：
      { "snapshot": <revision?>, "type": "Door", "within": {"center":[..],"radius":20},
        "where": {"interactable":true}, "similarTo":"废弃矿场", "limit":20,
        "consistency":"eventual" }
    返回：ids + revision + tick + indexWatermarks + truncated + pageToken + reasons。
    P1 未实现：向量 provider（similarTo 非空且 provider 缺席时显式 unavailable）。
    """
    spec = QuerySpec(
        type=params.get("type"),
        within=_within_from(params.get("within")),
        where=params.get("where") or {},
        similarTo=params.get("similarTo"),
        limit=params.get("limit", 100),
        consistency=params.get("consistency", "strong"),
        pageToken=params.get("pageToken"),
    )
    if spec.similarTo and (planner.indexes.vector is None
                           or not planner.indexes.vector.available):
        return {
            "ok": False,
            "error": "vector-index-unavailable",
            "detail": "向量索引可裁剪/未配置；结构化查询可用，similarTo 不可用",
            "revision": planner.world.snapshot().revision,
        }
    result = planner.execute(spec)
    return {"ok": True, **result.__dict__}


def mcp_eve_world_snapshot(world: World, params: Dict[str, Any]) -> Dict[str, Any]:
    """eve_world_snapshot：导出绑定 revision 的一致快照（权威投影）。"""
    snap = world.snapshot()
    ids = params.get("ids") or []
    selected = {PersistentId(*_parse_pid(i)) for i in ids} if ids else None
    rows = []
    for pid, type_id, data in snap.iter_entities():
        if selected is not None and pid not in selected:
            continue
        rows.append({"id": str(pid), "type": type_id, "fields": dict(data)})
    return {"ok": True, "revision": snap.revision, "tick": snap.tick,
            "entities": rows}


def _parse_pid(s: str) -> Tuple[str, int]:
    domain, _, seq = s.rpartition(":")
    return domain, int(seq)


def _within_from(v: Any) -> Optional[Tuple[float, float, float]]:
    if not v:
        return None
    center = v.get("center", (0, 0))
    return (float(center[0]), float(center[1]), float(v["radius"]))
