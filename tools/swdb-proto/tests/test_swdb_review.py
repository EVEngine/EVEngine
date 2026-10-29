"""test_swdb_review.py — 维护者 review 复现的正式回归测试（原 repro_review.py）。

覆盖 review 指出的 13 项缺陷（15 个断言，含 3 个 subtests）：
快照隔离与不可变、完整 read_set CAS、首读版本、MCP 纯读依赖、
create 原子性（无部分发布）、Schema 字段契约、唯一性（事务内/更新）、
删除反向引用、type-only 查询、分页快照绑定。

运行方式与其它测试一致：python -m unittest discover -s tests
"""

import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from swdb import (  # noqa: E402
    ComponentDef, FieldDef, FieldType, QuerySpec, QueryStaleTokenError,
    SchemaError,
)
from swdb_tx import (  # noqa: E402
    Conflict, TransactionManager, mcp_eve_world_commit,
)
from test_swdb import make_world, make_planner  # noqa: E402


class ReviewRepros(unittest.TestCase):
    def setUp(self):
        self.w = make_world()
        self.m = TransactionManager(self.w)
        self.d = next(p for p, t, _ in self.w.snapshot().iter_entities()
                      if t == 'Door')
        self.c = next(p for p, t, _ in self.w.snapshot().iter_entities()
                      if t == 'Chest')

    # -- 快照隔离与不可变 ------------------------------------------------
    def test_snapshot_stable(self):
        s = self.w.snapshot()
        self.w.set_field(self.d, 'locked', True)
        self.assertFalse(s.get(self.d)[1]['locked'])

    def test_snapshot_cannot_mutate_world(self):
        v = self.w.field_version(self.d, 'locked')
        try:
            self.w.snapshot().get(self.d)[1]['locked'] = True
        except (TypeError, AttributeError):
            pass
        self.assertFalse(self.w.snapshot().get(self.d)[1]['locked'])
        self.assertEqual(v, self.w.field_version(self.d, 'locked'))

    # -- 完整 read_set CAS ------------------------------------------------
    def test_complete_read_set(self):
        a = self.m.begin('A')
        a.read(self.d, 'locked')
        a.write(self.c, 'gold', 99)
        self.w.set_field(self.d, 'locked', True)
        with self.assertRaises(Conflict):
            a.commit()

    def test_first_read_version(self):
        a = self.m.begin('A')
        _, old = a.read(self.c, 'gold')
        self.w.set_field(self.c, 'gold', 100)
        a.write(self.c, 'gold', old + 1)
        with self.assertRaises(Conflict):
            a.commit()

    def test_mcp_read_only_dependency(self):
        v = self.w.field_version(self.d, 'locked')
        self.w.set_field(self.d, 'locked', True)
        r = mcp_eve_world_commit(self.m, {
            'readSet': [{'pid': str(self.d), 'field': 'locked', 'version': v}],
            'commands': [{'op': 'set', 'pid': str(self.c),
                          'field': 'gold', 'value': 99}],
        })
        self.assertFalse(r['ok'])

    # -- 原子性：create 校验失败无部分发布 --------------------------------
    def test_atomic_invalid_second_create(self):
        a = self.m.begin('A')
        a.create('Chest')
        a.create('InvalidType')

        def state():
            return copy.deepcopy((self.w._entities, self.w._types,
                                  self.w._field_versions, self.w.revision,
                                  self.w._tombstones, self.m.audit.entries))

        before = state()
        with self.assertRaises(SchemaError):
            a.commit()
        self.assertEqual(before, state())

    # -- Schema 字段契约（事务写入不得绕过权限） ---------------------------
    def test_schema_permissions(self):
        for f, v in [('hp', 50), ('displayName', 'bad'), ('locked', 'not-bool')]:
            with self.subTest(field=f):
                a = self.m.begin('A')
                with self.assertRaises(SchemaError):
                    a.write(self.d, f, v)
                    a.commit()

    def test_mcp_permissions(self):
        with self.assertRaises(SchemaError):
            mcp_eve_world_commit(self.m, {
                'commands': [{'op': 'set', 'pid': str(self.d),
                              'field': 'hp', 'value': 1}],
            })

    # -- 唯一性：事务内 create 之间 + write 更新 --------------------------
    def unique_setup(self):
        self.w.schema.register(ComponentDef(
            'Spawn', (FieldDef('zone', FieldType.STRING, default=''),)))
        self.m.constraints.register_unique('dungeon', 'Spawn', 'zone')

    def test_unique_two_creates(self):
        self.unique_setup()
        a = self.m.begin('A')
        a.create('Spawn', 'dungeon', zone='boss')
        a.create('Spawn', 'dungeon', zone='boss')
        with self.assertRaises(Conflict):
            a.commit()

    def test_unique_update(self):
        self.unique_setup()
        self.w.create('Spawn', 'dungeon', zone='boss')
        p = self.w.create('Spawn', 'dungeon', zone='entry')
        a = self.m.begin('A')
        a.write(p, 'zone', 'boss')
        with self.assertRaises(Conflict):
            a.commit()

    # -- 删除反向引用 ------------------------------------------------------
    def test_delete_referenced(self):
        self.w.schema.register(ComponentDef(
            'Link', (FieldDef('target', FieldType.STRING, default=''),)))
        self.w.create('Link', target=str(self.d))
        self.m.constraints.register_reference('target', 'dungeon')
        a = self.m.begin('A')
        a.delete(self.d)
        with self.assertRaises(Conflict):
            a.commit()

    # -- 查询：type-only 全量起点、分页快照绑定 ---------------------------
    def test_type_only_query(self):
        self.assertEqual(3, len(make_planner(self.w)
                                 .execute(QuerySpec(type='Door')).ids))

    def test_pagination_snapshot(self):
        q = make_planner(self.w)
        first = q.execute(QuerySpec(type='Door',
                                    where={'interactable': True}, limit=1))
        self.w.delete(self.d)
        q.indexes.attr.rebuild()
        q.indexes.spatial.rebuild()
        try:
            second = q.execute(QuerySpec(type='Door',
                                         where={'interactable': True}, limit=1,
                                         pageToken=first.pageToken))
        except Exception:
            return
        self.assertEqual(first.revision, second.revision,
                         'must preserve snapshot or reject stale token')

    # =====================================================================
    # 第二轮 review（@sunxfancy）回归
    # =====================================================================

    # -- R1：重复读取保持首次版本（决策依据不可被二次读覆盖） ------------
    def test_repeat_read_keep_first_version(self):
        a = self.m.begin('A')
        _, old = a.read(self.c, 'gold')       # 首读 gold
        self.w.set_field(self.c, 'gold', 100)  # 外部写入
        _, again = a.read(self.c, 'gold')      # 二次读：返回 100 但不覆盖首版
        self.assertEqual(again, 100)
        a.write(self.c, 'gold', old + 1)       # 基于首读值写
        with self.assertRaises(Conflict):
            a.commit()

    # -- R2：唯一性在最终候选状态验证 -------------------------------------
    def test_two_updates_same_unique(self):
        self.unique_setup()
        p1 = self.w.create('Spawn', 'dungeon', zone='a')
        p2 = self.w.create('Spawn', 'dungeon', zone='b')
        a = self.m.begin('A')
        a.write(p1, 'zone', 'c')
        a.write(p2, 'zone', 'c')
        with self.assertRaises(Conflict):
            a.commit()

    def test_default_unique(self):
        self.unique_setup()
        a = self.m.begin('A')
        a.create('Spawn', 'dungeon')
        a.create('Spawn', 'dungeon')
        with self.assertRaises(Conflict):
            a.commit()

    # -- R3：引用字段更新必须指向存活实体 --------------------------------
    def test_reference_update_missing(self):
        self.w.schema.register(ComponentDef(
            'Link', (FieldDef('target', FieldType.STRING, default=''),)))
        lk = self.w.create('Link', target=str(self.d))
        self.m.constraints.register_reference('target', 'dungeon')
        a = self.m.begin('A')
        a.write(lk, 'target', 'dungeon:99999')
        with self.assertRaises(Conflict):
            a.commit()

    # -- R4：undo 是受控恢复，可重建含只读字段的权威状态 -----------------
    def test_delete_undo(self):
        a = self.m.begin('A')
        a.delete(self.d)
        a.commit()
        undo_tx = a.undo()
        undo_tx.commit()
        row = self.w.snapshot().get(self.d)
        self.assertIsNotNone(row)
        self.assertEqual(row[1]['hp'], 100)  # 只读 hp 完整恢复

    # -- R5：MCP 显式前置条件必须参与 CAS；未知类型拒绝 ------------------
    def test_mcp_explicit_precondition(self):
        v = self.w.field_version(self.d, 'locked')
        self.w.set_field(self.d, 'locked', True)  # 前置条件版本已过期
        r = mcp_eve_world_commit(self.m, {
            'commands': [{'op': 'set', 'pid': str(self.c),
                          'field': 'gold', 'value': 99}],
            'preconditions': [{'kind': 'field-version', 'pid': str(self.d),
                               'field': 'locked', 'version': v}],
        })
        self.assertFalse(r['ok'])

    def test_mcp_unknown_precondition(self):
        with self.assertRaises(SchemaError):
            mcp_eve_world_commit(self.m, {
                'commands': [],
                'preconditions': [{'kind': 'revision-gte', 'version': 5}],
            })

    # -- R6：分页 token 绑定查询指纹，跨查询复用必须拒绝 -----------------
    def test_cross_query_token(self):
        q = make_planner(self.w)
        door_page = q.execute(QuerySpec(type='Door', limit=1))
        self.assertIsNotNone(door_page.pageToken)
        try:
            q.execute(QuerySpec(type='Chest', limit=1,
                                pageToken=door_page.pageToken))
        except QueryStaleTokenError:
            return
        self.fail('cross-query token must be rejected')


if __name__ == '__main__':
    unittest.main(verbosity=2)
