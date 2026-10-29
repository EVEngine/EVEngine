"""test_swdb_p2.py — Semantic World Database P2（事务 / 多 Agent 冲突检测）验收测试。

覆盖 docs/dev/SemanticWorldDatabase架构设计.md §11 P2 验收点：
  - 两个 Agent 并发修改可复现成功合并、拒绝和重试；
  - 运行时写入参与检测（隐藏写者无法绕过冲突检测）；
  - 注入 owner 失败时无部分发布。
外加：phantom 冲突、唯一性约束、条件 undo、abort、MCP commit 契约。
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swdb import ComponentDef, FieldDef, FieldType, SchemaRegistry, World  # noqa: E402
from swdb_tx import (  # noqa: E402
    Conflict, TransactionManager, mcp_eve_world_commit,
)


def make_world() -> World:
    reg = SchemaRegistry()
    reg.register(ComponentDef(
        "Door",
        (
            FieldDef("position", FieldType.VEC3, default=[0.0, 0.0, 0.0],
                     owner="transform"),
            FieldDef("locked", FieldType.BOOL, default=False),
            FieldDef("hp", FieldType.INT, default=100, readonly=True,
                     owner="combat"),
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
    reg.register(ComponentDef(
        "SpawnPoint",
        (
            FieldDef("zone", FieldType.STRING, default="dungeon"),
            FieldDef("active", FieldType.BOOL, default=True),
        ),
    ))
    w = World(reg)
    w.create("Door", domain="dungeon", position=[5.0, 5.0, 0.0], locked=False)
    w.create("Door", domain="dungeon", position=[30.0, 1.0, 0.0], locked=True)
    w.create("Chest", domain="dungeon", position=[6.0, 5.0, 0.0], gold=42)
    w.create("SpawnPoint", domain="dungeon", zone="boss", active=True)
    return w


def door(w: World):
    return next(pid for pid, t, _ in w.snapshot().iter_entities()
                if t == "Door")


def chest(w: World):
    return next(pid for pid, t, _ in w.snapshot().iter_entities()
                if t == "Chest")


def spawn(w: World):
    return next(pid for pid, t, _ in w.snapshot().iter_entities()
                if t == "SpawnPoint")


class TestConcurrentSameField(unittest.TestCase):
    """P2 验收 1：同字段并发修改 → 一个成功、一个拒绝、重试后成功。"""

    def test_conflict_then_retry_succeeds(self):
        w = make_world()
        mgr = TransactionManager(w)
        d = door(w)

        # Agent A 与 Agent B 同时读到 locked=False（同 base revision）
        base = w.revision
        tx_a = mgr.begin("agent-A", base_revision=base)
        tx_b = mgr.begin("agent-B", base_revision=base)
        tx_a.read(d, "locked")
        tx_b.read(d, "locked")
        tx_a.write(d, "locked", True)
        tx_b.write(d, "locked", True)

        # A 先提交：成功
        r_a = tx_a.commit()
        self.assertTrue(r_a["ok"])
        self.assertEqual(w.snapshot().get(d)[1]["locked"], True)

        # B 提交：版本不匹配 → 结构化冲突，世界未被 B 改变
        with self.assertRaises(Conflict) as ctx:
            tx_b.commit()
        self.assertEqual(ctx.exception.kind, "version-mismatch")
        self.assertEqual(ctx.exception.detail["field"], "locked")
        # 字段被 A 改过：当前版本必须大于 B 读到的期望版本
        self.assertGreater(ctx.exception.detail["currentVersion"],
                           ctx.exception.detail["expectedVersion"])
        self.assertIsNotNone(ctx.exception.retry_hint)
        self.assertEqual(ctx.exception.actor, "agent-B")

        # B 按 retry_hint 重试：重新读新版本后提交成功
        tx_b2 = mgr.begin("agent-B")
        v, _ = tx_b2.read(d, "locked")
        self.assertEqual(v, r_a["revision"])
        tx_b2.write(d, "locked", False)
        self.assertTrue(tx_b2.commit()["ok"])
        self.assertEqual(w.snapshot().get(d)[1]["locked"], False)


class TestDisjointMerge(unittest.TestCase):
    """P2 验收 1b：不相交字段并发修改 → 自动合并（读集/写集兼容）。"""

    def test_disjoint_fields_merge(self):
        w = make_world()
        mgr = TransactionManager(w)
        d, c = door(w), chest(w)
        base = w.revision

        tx_a = mgr.begin("agent-A", base_revision=base)
        tx_b = mgr.begin("agent-B", base_revision=base)
        tx_a.read(d, "locked")
        tx_b.read(c, "gold")
        tx_a.write(d, "locked", True)
        tx_b.write(c, "gold", 100)

        self.assertTrue(tx_a.commit()["ok"])
        self.assertTrue(tx_b.commit()["ok"])  # 不相交字段，互不冲突
        self.assertEqual(w.snapshot().get(d)[1]["locked"], True)
        self.assertEqual(w.snapshot().get(c)[1]["gold"], 100)


class TestRuntimeWriteDetection(unittest.TestCase):
    """P2 验收 2：运行时直写是"隐藏写者"，必须使进行中的事务失效。"""

    def test_runtime_write_invalidates_tx(self):
        w = make_world()
        mgr = TransactionManager(w)
        d = door(w)

        tx = mgr.begin("agent-A")
        tx.read(d, "locked")
        tx.write(d, "locked", True)

        # 引擎/游戏系统直写同一字段（绕过事务）
        w.set_field(d, "locked", True)

        with self.assertRaises(Conflict) as ctx:
            tx.commit()
        self.assertEqual(ctx.exception.kind, "version-mismatch")

    def test_runtime_write_other_field_does_not_block(self):
        w = make_world()
        mgr = TransactionManager(w)
        c = chest(w)

        tx = mgr.begin("agent-A")
        tx.read(c, "gold")
        tx.write(c, "gold", 1)
        # 直写不相干字段（hp 是 runtime owner，但事务没碰它）
        d = door(w)
        w.set_field(d, "locked", True)
        self.assertTrue(tx.commit()["ok"])


class TestPhantom(unittest.TestCase):
    """P2 验收 2b：范围谓词 phantom——base 后落入范围内的新对象使旧事务失效。"""

    def test_phantom_detected(self):
        w = make_world()
        mgr = TransactionManager(w)

        tx_a = mgr.begin("agent-A")
        tx_a.read_range((5.0, 5.0, 2.0))  # 决策依赖该范围（例如"范围内没有守卫"）

        # Agent B 在该范围内创建新实体并提交
        tx_b = mgr.begin("agent-B")
        tx_b.create("Chest", domain="dungeon", position=[5.1, 5.1, 0.0], gold=7)
        self.assertTrue(tx_b.commit()["ok"])

        # A 提交：phantom 冲突（决策前提已被 B 改变）
        with self.assertRaises(Conflict) as ctx:
            tx_a.commit()
        self.assertEqual(ctx.exception.kind, "phantom")
        self.assertIn("newEntity", ctx.exception.detail)
        self.assertIsNotNone(ctx.exception.retry_hint)


class TestUniqueConstraint(unittest.TestCase):
    """P2 验收 3：领域约束——区域内唯一性（如唯一出生点）。"""

    def test_unique_violation_rejected(self):
        w = make_world()
        mgr = TransactionManager(w)
        mgr.constraints.register_unique("dungeon", "SpawnPoint", "zone")

        tx = mgr.begin("agent-A")
        tx.create("SpawnPoint", domain="dungeon", zone="boss", active=True)
        with self.assertRaises(Conflict) as ctx:
            tx.commit()
        self.assertEqual(ctx.exception.kind, "unique-violation")
        self.assertIn("existing", ctx.exception.detail)
        # 世界未变化：新 SpawnPoint 未落盘
        self.assertEqual(
            sum(1 for _pid, t, _ in w.snapshot().iter_entities()
                if t == "SpawnPoint"), 1)

    def test_unique_different_key_ok(self):
        w = make_world()
        mgr = TransactionManager(w)
        mgr.constraints.register_unique("dungeon", "SpawnPoint", "zone")
        tx = mgr.begin("agent-A")
        tx.create("SpawnPoint", domain="dungeon", zone="entrance", active=True)
        self.assertTrue(tx.commit()["ok"])


class TestOwnerFailureAtomicity(unittest.TestCase):
    """P2 验收 3b：注入 owner 失败 → 无部分发布（世界零变化），修复后可重试。"""

    def test_owner_failure_no_partial_publish(self):
        w = make_world()
        mgr = TransactionManager(w)
        c = chest(w)
        # 注册 owner 验证器：gold 属于 economy，注入一次性失败
        calls = {"n": 0}

        def flaky_owner(pid, field, value):
            calls["n"] += 1
            if calls["n"] == 1:
                raise Conflict("owner-rejected",
                               {"owner": "economy", "field": field})

        mgr.constraints.register_owner_validator("economy", flaky_owner)

        tx = mgr.begin("agent-A")
        tx.write(c, "gold", 999)
        before_rev = w.revision
        with self.assertRaises(Conflict) as ctx:
            tx.commit()
        self.assertEqual(ctx.exception.kind, "owner-rejected")
        # 原子性：世界 revision 与内容零变化
        self.assertEqual(w.revision, before_rev)
        self.assertEqual(w.snapshot().get(c)[1]["gold"], 42)

        # owner 恢复后（验证器不再失败），同事务重试成功
        tx2 = mgr.begin("agent-A")
        tx2.write(c, "gold", 999)
        self.assertTrue(tx2.commit()["ok"])
        self.assertEqual(w.snapshot().get(c)[1]["gold"], 999)


class TestConditionalUndo(unittest.TestCase):
    """P2 验收 4：undo 是带当前前置条件的新事务，不是整世界回滚。"""

    def test_undo_restores_old_value(self):
        w = make_world()
        mgr = TransactionManager(w)
        d = door(w)

        tx = mgr.begin("agent-A")
        tx.write(d, "locked", True)
        tx.commit()
        self.assertEqual(w.snapshot().get(d)[1]["locked"], True)

        undo = tx.undo()
        self.assertTrue(undo.commit()["ok"])
        self.assertEqual(w.snapshot().get(d)[1]["locked"], False)

    def test_undo_create_deletes_entity(self):
        w = make_world()
        mgr = TransactionManager(w)

        tx = mgr.begin("agent-A")
        pid = tx.create("Chest", domain="dungeon", position=[1.0, 1.0, 0.0],
                        gold=5)
        tx.commit()
        self.assertIsNotNone(w.snapshot().get(pid))

        undo = tx.undo()
        self.assertTrue(undo.commit()["ok"])
        self.assertIsNone(w.snapshot().get(pid))  # 重建的实体被撤销删除

    def test_undo_delete_restores_entity(self):
        w = make_world()
        mgr = TransactionManager(w)
        c = chest(w)

        tx = mgr.begin("agent-A")
        tx.read(c, "gold")  # 删除前快照
        tx.delete(c)
        tx.commit()
        self.assertIsNone(w.snapshot().get(c))

        undo = tx.undo()
        self.assertTrue(undo.commit()["ok"])
        row = w.snapshot().get(c)
        self.assertIsNotNone(row)
        self.assertEqual(row[1]["gold"], 42)  # 原值恢复


class TestAbort(unittest.TestCase):
    def test_aborted_tx_cannot_commit(self):
        w = make_world()
        mgr = TransactionManager(w)
        c = chest(w)
        tx = mgr.begin("agent-A")
        tx.write(c, "gold", 1)
        tx.abort("manual")
        with self.assertRaises(RuntimeError):
            tx.commit()
        self.assertEqual(w.snapshot().get(c)[1]["gold"], 42)


class TestMcpCommitContract(unittest.TestCase):
    """P2 验收 5：MCP eve_world_commit 契约形状。"""

    def test_commit_success_shape(self):
        w = make_world()
        mgr = TransactionManager(w)
        d = door(w)
        r = mcp_eve_world_commit(mgr, {
            "actor": "agent-A",
            "commands": [
                {"op": "set", "pid": str(d), "field": "locked", "value": True},
            ],
        })
        self.assertTrue(r["ok"])
        for key in ("txId", "revision", "diff", "writes"):
            self.assertIn(key, r)
        self.assertEqual(w.snapshot().get(d)[1]["locked"], True)

    def test_commit_conflict_shape(self):
        w = make_world()
        mgr = TransactionManager(w)
        d = door(w)
        old_v = w.field_version(d, "locked")  # B 提交前的字段版本
        # Agent B 先提交，改掉 locked（字段版本前进）
        tx = mgr.begin("agent-B")
        tx.write(d, "locked", False)
        tx.commit()
        # Agent A 带着过期的 readSet 提交 → 必须冲突
        r = mcp_eve_world_commit(mgr, {
            "actor": "agent-A",
            "readSet": [{"pid": str(d), "field": "locked", "version": old_v}],
            "commands": [
                {"op": "set", "pid": str(d), "field": "locked", "value": True},
            ],
        })
        self.assertFalse(r["ok"])
        self.assertEqual(r["conflict"]["kind"], "version-mismatch")
        self.assertIn("retryHint", r["conflict"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
