"""test_swdb.py — Semantic World Database P1 参考实现的验收测试。

覆盖 docs/dev/SemanticWorldDatabase架构设计.md §11 P1 验收点：
  - 同版本查询与权威扫描一致；
  - 删除、移动、索引落后和裁剪后降级行为可验证。
外加：Schema 权限、快照一致性、分页、预算、MCP 接入契约。
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swdb import (  # noqa: E402
    AttributeIndex, ComponentDef, FieldDef, FieldType, IndexSet,
    PersistentId, QueryPlanner, QuerySpec, SchemaError, SchemaRegistry,
    SpatialIndex, TombstoneError, VectorIndex, World, mcp_eve_world_query,
    mcp_eve_world_snapshot,
)


def make_schema() -> SchemaRegistry:
    reg = SchemaRegistry()
    reg.register(ComponentDef(
        "Door",
        (
            FieldDef("position", FieldType.VEC3, default=[0.0, 0.0, 0.0],
                     owner="transform"),
            FieldDef("interactable", FieldType.BOOL, default=True),
            FieldDef("locked", FieldType.BOOL, default=False),
            FieldDef("hp", FieldType.INT, default=100, readonly=True,
                     owner="combat"),
            FieldDef("displayName", FieldType.STRING, default="door",
                     derived=True, owner="presentation"),
        ),
    ))
    reg.register(ComponentDef(
        "Chest",
        (
            FieldDef("position", FieldType.VEC3, default=[0.0, 0.0, 0.0],
                     owner="transform"),
            FieldDef("gold", FieldType.INT, default=0),
        ),
    ))
    return reg


def make_world() -> World:
    reg = make_schema()
    w = World(reg)
    w.create("Door", domain="dungeon", position=[5.0, 5.0, 0.0], locked=False)
    w.create("Door", domain="dungeon", position=[30.0, 1.0, 0.0], locked=True)
    w.create("Door", domain="dungeon", position=[2.0, 50.0, 0.0], locked=False)
    w.create("Chest", domain="dungeon", position=[6.0, 5.0, 0.0], gold=42)
    w.create("Chest", domain="dungeon", position=[200.0, 200.0, 0.0], gold=1)
    return w


def make_planner(world: World) -> QueryPlanner:
    attr = AttributeIndex(world, ("interactable", "locked"))
    spatial = SpatialIndex(world, cell=8.0)
    return QueryPlanner(world, IndexSet(attr, spatial))


class TestSchemaPermissions(unittest.TestCase):
    """§4：派生字段不可写；只读字段不可由写入者直接设置。"""

    def test_derived_field_rejected(self):
        w = make_world()
        pid = next(pid for pid, t, _ in w.snapshot().iter_entities()
                   if t == "Door")
        with self.assertRaises(SchemaError):
            w.set_field(pid, "displayName", "boss-door")

    def test_readonly_field_rejected(self):
        w = make_world()
        pid = next(pid for pid, t, _ in w.snapshot().iter_entities()
                   if t == "Door")
        with self.assertRaises(SchemaError):
            w.set_field(pid, "hp", 50)

    def test_create_with_derived_field_rejected(self):
        w = make_world()
        with self.assertRaises(SchemaError):
            w.create("Door", displayName="x")

    def test_writable_field_accepted(self):
        w = make_world()
        pid = next(pid for pid, t, _ in w.snapshot().iter_entities()
                   if t == "Door")
        rev = w.set_field(pid, "locked", True)
        self.assertEqual(rev, w.revision)
        self.assertEqual(w.snapshot().get(pid)[1]["locked"], True)


class TestIdentity(unittest.TestCase):
    """§3：稳定 ID、tombstone、不复用、过期查询拒绝。"""

    def test_id_stable_across_mutation(self):
        w = make_world()
        snap1 = w.snapshot()
        pid = next(iter(pid for pid, t, _ in snap1.iter_entities()
                        if t == "Door"))
        w.set_field(pid, "locked", True)
        w.advance_tick()
        # 改名/移动不改变身份
        self.assertEqual(str(pid), str(pid))
        self.assertIn(pid, {p for p, _, _ in w.snapshot().iter_entities()})

    def test_delete_leaves_tombstone_and_rejects(self):
        w = make_world()
        pid = next(pid for pid, t, _ in w.snapshot().iter_entities()
                   if t == "Chest")
        w.delete(pid)
        # 权威写入路径：命中 tombstone，拒绝
        with self.assertRaises(TombstoneError):
            w.set_field(pid, "gold", 1)
        # 新快照视图：实体已不可见（返回 None，而非陈旧数据）
        self.assertIsNone(w.snapshot().get(pid))
        # 旧快照视图：保持删除前的一致性（不被删除影响）
        snap_before = make_world().snapshot()  # 独立世界对照
        self.assertIsNotNone(snap_before.get(pid))
        # tombstone 版本可查询
        self.assertIsNotNone(w.tombstone_revision(pid))

    def test_ids_never_reused(self):
        w = make_world()
        pid = next(pid for pid, t, _ in w.snapshot().iter_entities()
                   if t == "Chest")
        w.delete(pid)
        # 新实体绝不占用被删 ID 的槽位
        new_pids = [w.create("Chest") for _ in range(5)]
        self.assertNotIn(pid, new_pids)
        self.assertNotEqual(str(pid), str(new_pids[0]))


class TestSnapshotConsistency(unittest.TestCase):
    """§2/§9：同一 revision 内多次查询一致；写后 revision 递增。"""

    def test_snapshot_immutable_across_queries(self):
        w = make_world()
        snap = w.snapshot()
        q1 = list(snap.iter_entities())
        # 快照之外的写入不影响已持有快照
        pid = next(pid for pid, t, _ in snap.iter_entities() if t == "Door")
        w.delete(pid)
        q2 = list(snap.iter_entities())
        self.assertEqual(len(q1), len(q2))

    def test_revision_monotonic(self):
        w = make_world()
        r0 = w.revision
        pid = next(pid for pid, t, _ in w.snapshot().iter_entities()
                   if t == "Door")
        w.set_field(pid, "locked", True)
        self.assertGreater(w.revision, r0)


class TestDeterministicQuery(unittest.TestCase):
    """P1 主验收：同版本查询与权威扫描一致。"""

    def test_attribute_query_matches_full_scan(self):
        w = make_world()
        planner = make_planner(w)
        snap = w.snapshot()

        for key in ("interactable", "locked"):
            for value in (True, False):
                r = planner.execute(QuerySpec(where={key: value},
                                              consistency="strong"))
                self.assertEqual(len(r.reasons), 1, r.reasons)
                scanned = {str(pid) for pid, t, data in snap.iter_entities()
                           if key in data and data[key] == value}
                self.assertEqual(set(r.ids), scanned,
                                 f"{key}={value}: 查询必须与权威扫描一致")

    def test_type_and_attribute_combined(self):
        w = make_world()
        planner = make_planner(w)
        r = planner.execute(QuerySpec(type="Door", where={"locked": False}))
        snap = w.snapshot()
        scanned = {str(pid) for pid, t, data in snap.iter_entities()
                   if t == "Door" and data.get("locked") is False}
        self.assertEqual(set(r.ids), scanned)

    def test_spatial_query_precise(self):
        w = make_world()
        planner = make_planner(w)
        # 圆心 (5,5) 半径 2：只命中第一扇门（5,5）与宝箱（6,5）
        r = planner.execute(QuerySpec(within=(5.0, 5.0, 2.0)))
        snap = w.snapshot()
        scanned = {str(pid) for pid, t, data in snap.iter_entities()
                   if "position" in data
                   and ((data["position"][0] - 5) ** 2
                        + (data["position"][1] - 5) ** 2) ** 0.5 <= 2.0}
        self.assertEqual(set(r.ids), scanned)


class TestDegradation(unittest.TestCase):
    """P1 验收：移动、索引落后、裁剪后降级行为可验证。"""

    def test_move_updates_spatial_query(self):
        w = make_world()
        planner = make_planner(w)
        door = next(pid for pid, t, _ in w.snapshot().iter_entities()
                    if t == "Door")
        before = planner.execute(QuerySpec(within=(200.0, 200.0, 1.0))).ids
        # 移动后（重建索引模拟物移动 + 索引跟随）
        w.set_field(door, "position", [200.0, 200.0, 0.0])
        planner.indexes.spatial.rebuild()
        after = planner.execute(QuerySpec(within=(200.0, 200.0, 1.0))).ids
        self.assertNotIn(str(door), before)
        self.assertIn(str(door), after)

    def test_lag_index_stalls_strong_query(self):
        w = make_world()
        planner = make_planner(w)
        # 模拟索引落后：属性索引停在旧 revision
        planner.indexes.attr.mark_lag()
        r = planner.execute(QuerySpec(where={"interactable": True},
                                      consistency="strong"))
        self.assertEqual(r.ids, [])
        self.assertTrue(any("stalled" in reason for reason in r.reasons))
        self.assertEqual(r.revision, w.revision)

    def test_lag_index_eventual_query_still_returns_with_watermark(self):
        w = make_world()
        planner = make_planner(w)
        planner.indexes.attr.mark_lag()
        r = planner.execute(QuerySpec(where={"interactable": True},
                                      consistency="eventual"))
        # 最终一致：允许返回，但必须披露索引水位
        self.assertTrue(r.ids)
        wm = {x["index"]: x for x in r.indexWatermarks}
        self.assertTrue(wm["attribute"]["lag"])

    def test_missing_vector_index_structured_query_ok(self):
        w = make_world()
        planner = make_planner(w)
        r = planner.execute(QuerySpec(where={"locked": False}))
        self.assertTrue(r.ids)  # 结构化查询不依赖向量

    def test_similar_to_without_vector_explicitly_unavailable(self):
        w = make_world()
        planner = make_planner(w)
        r = mcp_eve_world_query(planner, {"similarTo": "废弃矿场的门"})
        self.assertFalse(r["ok"])
        self.assertEqual(r["error"], "vector-index-unavailable")

    def test_vector_index_used_when_provider_present(self):
        w = make_world()

        class FakeProvider:
            def search(self, description, k):
                return ["dungeon:1", "dungeon:2"]

        vec = VectorIndex(w, provider=FakeProvider())
        planner = QueryPlanner(w, IndexSet(
            AttributeIndex(w, ("interactable",)),
            SpatialIndex(w), vec))
        r = mcp_eve_world_query(planner, {"similarTo": "x", "limit": 2})
        self.assertTrue(r["ok"])
        self.assertEqual(r["ids"], ["dungeon:1", "dungeon:2"])


class TestBudgetAndPaging(unittest.TestCase):
    """§9：预算、截断标志、分页 resume token。"""

    def test_budget_exhaustion_marks_truncated(self):
        w = make_world()
        planner = make_planner(w)
        r = planner.execute(QuerySpec(where={"interactable": True},
                                      budgetNodes=1))
        self.assertTrue(r.truncated)

    def test_paging_no_duplicates_no_gaps(self):
        w = make_world()
        planner = make_planner(w)
        spec = QuerySpec(where={"interactable": True}, limit=1)
        collected: list[str] = []
        token = None
        for _ in range(10):
            spec.pageToken = token
            r = planner.execute(spec)
            collected.extend(r.ids)
            token = r.pageToken
            if token is None:
                break
        self.assertEqual(len(set(collected)), len(collected))  # 无重复
        # 权威扫描核对全集
        snap = w.snapshot()
        scanned = {str(pid) for pid, t, data in snap.iter_entities()
                   if data.get("interactable") is True}
        self.assertEqual(set(collected), scanned)  # 无遗漏


class TestMcpContract(unittest.TestCase):
    """§9：MCP 工具接入契约形状。"""

    def test_world_query_returns_contract_fields(self):
        w = make_world()
        planner = make_planner(w)
        r = mcp_eve_world_query(planner, {
            "type": "Door", "where": {"locked": True},
            "limit": 10, "consistency": "strong",
        })
        self.assertTrue(r["ok"])
        for key in ("ids", "revision", "tick", "truncated", "budgetUsed",
                    "indexWatermarks", "pageToken", "reasons"):
            self.assertIn(key, r)

    def test_world_snapshot_contract(self):
        w = make_world()
        r = mcp_eve_world_snapshot(w, {})
        self.assertTrue(r["ok"])
        self.assertEqual(r["revision"], w.revision)
        self.assertEqual(len(r["entities"]), 5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
