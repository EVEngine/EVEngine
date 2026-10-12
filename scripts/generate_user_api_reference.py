#!/usr/bin/env python3
"""Generate supplementary user references from the source binding catalogue.

First run generate_binding_contracts.py --json-output <catalogue>, then this
script --catalogue <catalogue>. Narrative chapters remain hand maintained.
Only chapters with an api-reference.md topic opt in. --check detects drift.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import check_bindings
import utf8_stdio


def render(module: str, contracts: list[dict], names: set[str]) -> str:
    lines = [f"# {module}：补充绑定参考", "",
             "本页由当前源码的绑定目录生成，补充主章尚未逐项解释的入口。先读主章的任务流程、",
             "初始化条件和生命周期，再查本页；本页不代表每个设备或平台都已执行这些 API。",
             "`dynamic` 表示静态提取器无法表达具体脚本投影，Result/异常/借用语义以绑定实现为准。",
             "参数列没有默认值：不要从 C++ 默认实参推断脚本实参可省略。", ""]
    entries: dict[str, list[dict]] = {}
    for contract in contracts:
        if contract["method"] in names:
            entries.setdefault(contract["scriptClass"] or "接收者见绑定源码", []).append(contract)
    seen: set[str] = set()
    for receiver, records in sorted(entries.items()):
        lines += [f"## {receiver}", ""]
        for record in sorted(records, key=lambda item: (item["method"], item["source"])):
            parameters = ", ".join(f"{p['name']}: {p['type']}" for p in record["parameters"])
            signature = f"{record['method']}({parameters})"
            source, _, number = record["source"].rpartition(":")
            lines += [f"- `{signature}` → `{record['returnType']}`；"
                      f"[绑定实现 L{number}](../../../../{source}#L{number})。"]
            seen.add(record["method"])
        lines.append("")
    for name in sorted(names - seen):
        lines += [f"- `{name}`：属性或动态绑定；查主章链接的模块源码，不能当成无参数函数。"]
    lines += ["", "重建：`python3 scripts/generate_binding_contracts.py --json-output eve-api.json`，",
              "再运行 `python3 scripts/generate_user_api_reference.py --catalogue eve-api.json`。", ""]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--catalogue", required=True, type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    catalogue = json.loads(args.catalogue.read_text(encoding="utf-8"))["contracts"]
    changed = 0
    for module, directory, skip in check_bindings.iter_modules():
        chapter = check_bindings.doc_for(module)
        if chapter is None:
            continue
        reference = chapter.with_suffix("") / "api-reference.md"
        if not reference.exists():
            continue
        prose = chapter.read_text(encoding="utf-8")
        for topic in reference.parent.rglob("*.md"):
            if topic != reference:
                prose += topic.read_text(encoding="utf-8")
        names = {name for name in check_bindings.bound_names(directory, skip) if name not in prose}
        sources = {path.relative_to(check_bindings.ROOT).as_posix()
                   for path in check_bindings.iter_module_cpp(directory, skip)}
        records = [item for item in catalogue if item["source"].rpartition(":")[0] in sources]
        content = render(module, records, names)
        if reference.read_text(encoding="utf-8") != content:
            changed += 1
            if not args.check:
                reference.write_text(content, encoding="utf-8")
            print(f"{'stale' if args.check else 'updated'} {reference.relative_to(check_bindings.ROOT)}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    raise SystemExit(main())
