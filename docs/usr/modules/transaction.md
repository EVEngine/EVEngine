# 事务：计划记录与真正的原子提交

**入口：** `eve.Transaction()`，槽位 `transaction`。脚本 ledger/plan 记录操作与状态；
它们不会看到字符串 `debit` 就自动扣款，也不会自动执行任意游戏回调。
真正的跨域原子操作由拥有参与者的领域入口或 C++ Coordinator 管理。

## 任务一：建立并验证一份计划

初始化或显式命令处理中执行，配置需 transaction。

```squirrel
function transaction_value(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
local ledger = transaction.newLedger();
local created = transaction_value(ledger.create("campaign", "command",
    "00000000-0000-4000-8000-000000000001"));
local plan = ledger.find(created.id);
local operation = transaction_value(plan.stage("change_control", "base-4",
    "{\"owner\":\"faction-b\"}", "00000000-0000-4000-8000-000000000002"));
transaction_value(plan.markValid(operation));
transaction_value(plan.validate());
transaction_value(plan.commit());
assert(plan.getState() == "committed");
```

markValid 在真实项目中必须来自领域校验，不能无条件照抄来绕过规则。此例演示计划协议，
没有移动基地所有权。提交后 plan 冻结，不能追加操作或声称 rollback 会撤销任意外部副作用。

## 任务二：恢复计划与审计结果

```squirrel
local copy = transaction.newLedger();
transaction_value(copy.restore(ledger.snapshotJson()));
assert(copy.at(0).getState() == "committed");
assert(copy.at(0).operationAt(0).getKind() == "change_control");
```

事务 UUID 和 operation UUID 要唯一且稳定；correlation/causation 追踪业务链。
ledger 拥有 plan/operation/event，find 与 at 是借用访问；restore 或销毁后重新查询。
保存 ledger 不等于保存已改变的各领域状态。失败信息、terminal 状态和部分提交风险要
向调用者显示，不能统一当成成功或简单 false。

## 跨域购买/生产的接法

以“扣钱并放入背包”为例：在提交前准备账本和背包的候选，验证余额、容量、定义与版本；
全部准备成功才原子发布。失败必须使双方旧状态保持不变。参与者在同一 owner 线程，
同步调用期间借用上下文，不能保留临时指针或在锁内执行未知脚本。

C++ Coordinator 的 prepare/commit/rollback/compensate 契约见
[Transaction.h](../../../src/modules/transaction/Transaction.h)；重要区别是补偿不是自动
原子回滚，PartiallyCommitted/CompensationFailed 等必须被显式处理。
直接使用已有 [RPG 商店事务](../../../test/rpg_shop_transaction.cpp) 或域公开入口，
不要给每个脚本作者留一个“先执行，再自己撤销”的错误模板。

源码：[Transaction.cpp](../../../src/modules/transaction/Transaction.cpp)。
协议脚本证据：[transaction.cpp](../../../test/transaction.cpp)，
参与者失败注入：[transaction_participant.cpp](../../../test/transaction_participant.cpp)。

## 补充绑定参考

参数与接收者见 [源码生成参考](transaction/api-reference.md)，使用前先确认本章的前置条件与生命周期。

## 脚本事务组合

脚本可用 `transaction.newAtomicBatch()` 组合真实领域参与者，并通过 `batch.commit()` 提交。

生命周期、费用合并与完整示例见 [玩法事务组合](../GAMEPLAY_PAYMENT_COMPOSITION.md#script-composition)。
