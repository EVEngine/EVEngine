#!/usr/bin/env python3
"""Generate EveScript Binding Contracts from SimpleSquirrel addFunc declarations.

The generator reads the same C++ declarations that bind functions through
SimpleSquirrel. Member-function parameter names come from their class
declarations; lambda parameters come from the binding expression itself. A
binding that cannot supply real parameter names is rejected instead of
silently publishing arg0/arg1 placeholders.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass, field
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "src" / "engine", ROOT / "src" / "modules")
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp"}


@dataclass
class Parameter:
    name: str
    cpp_type: str
    script_type: str
    nullable: bool = False
    default: str = ""
    unit: str = "None"
    choices: list[str] = field(default_factory=list)


@dataclass
class Contract:
    module: str
    script_class: str
    method: str
    parameters: list[Parameter]
    return_type: str
    return_nullable: bool
    ownership: str
    thread_affinity: str
    platforms: list[str]
    documentation_id: str
    source: str

    @property
    def key(self) -> str:
        owner = f"{self.script_class}." if self.script_class else ""
        return f"{self.module}/{owner}{self.method}"


def mask_comments(source: str) -> str:
    output = list(source)
    quote: str | None = None
    line_comment = False
    block_comment = False
    i = 0
    while i < len(source):
        value = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if line_comment:
            if value == "\n":
                line_comment = False
            else:
                output[i] = " "
        elif block_comment:
            if value == "*" and nxt == "/":
                output[i] = output[i + 1] = " "
                i += 1
                block_comment = False
            elif value != "\n":
                output[i] = " "
        elif quote:
            if value == "\\":
                output[i] = " "
                if i + 1 < len(output):
                    output[i + 1] = " "
                    i += 1
            elif value == quote:
                quote = None
        elif value in {'"', "'"}:
            quote = value
        elif value == "/" and nxt == "/":
            output[i] = output[i + 1] = " "
            i += 1
            line_comment = True
        elif value == "/" and nxt == "*":
            output[i] = output[i + 1] = " "
            i += 1
            block_comment = True
        i += 1
    return "".join(output)


def matching(source: str, opening: int, left: str = "(", right: str = ")") -> int | None:
    depth = 0
    quote: str | None = None
    i = opening
    while i < len(source):
        value = source[i]
        nxt = source[i + 1] if i + 1 < len(source) else ""
        if quote:
            if value == "\\":
                i += 2
                continue
            if value == quote:
                quote = None
        elif value in {'"', "'"}:
            quote = value
        elif value == "/" and nxt == "/":
            newline = source.find("\n", i + 2)
            i = len(source) if newline < 0 else newline
            continue
        elif value == "/" and nxt == "*":
            end = source.find("*/", i + 2)
            i = len(source) if end < 0 else end + 2
            continue
        elif value == left:
            depth += 1
        elif value == right:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return None


def split_top_level(value: str, delimiter: str = ",") -> list[str]:
    result: list[str] = []
    begin = 0
    depths = {"(": 0, "[": 0, "{": 0, "<": 0}
    pairs = {")": "(", "]": "[", "}": "{", ">": "<"}
    quote: str | None = None
    i = 0
    while i < len(value):
        current = value[i]
        if quote:
            if current == "\\":
                i += 2
                continue
            if current == quote:
                quote = None
        elif current in {'"', "'"}:
            quote = current
        elif current in depths:
            depths[current] += 1
        elif current in pairs:
            depths[pairs[current]] = max(0, depths[pairs[current]] - 1)
        elif current == delimiter and not any(depths.values()):
            result.append(value[begin:i].strip())
            begin = i + 1
        i += 1
    result.append(value[begin:].strip())
    return [item for item in result if item]


def script_type(cpp_type: str) -> tuple[str, bool]:
    normalized = re.sub(r"\b(const|volatile|class|struct)\b", "", cpp_type)
    normalized = re.sub(r"\s+", " ", normalized).strip()
    nullable = "*" in normalized or "optional<" in normalized
    plain = normalized.replace("&", "").replace("*", "").strip()
    plain = re.sub(r"^(std::|ssq::|eve::)", "", plain)
    if plain in {"void"}:
        return "void", False
    if plain in {"bool", "SQBool"}:
        return "bool", nullable
    if re.search(r"(^|::)(u?int\d*_t|size_t|int|long|short|unsigned|SQInteger)$", plain):
        return "int", nullable
    if plain in {"float", "double", "SQFloat"}:
        return "float", nullable
    if "string" in plain or plain in {"char", "SQChar"}:
        return "string", nullable
    if re.search(r"(^|::)(vector|array|Array)<", plain) or plain == "Array":
        return "Array<dynamic>", nullable
    if re.search(r"(^|::)(map|unordered_map|Table)<", plain) or plain == "Table":
        return "Table<string, dynamic>", nullable
    if plain in {"Object", "Function", "Class", "Instance"}:
        return "dynamic", nullable
    if re.fullmatch(r"[TUVW]", plain):
        return "dynamic", nullable
    match = re.search(r"([A-Za-z_]\w*)\s*(?:<.*>)?$", plain)
    return (match.group(1) if match else "dynamic"), nullable


def parse_parameter(text: str, index: int) -> Parameter | None:
    text = re.sub(r"\[\[[^]]*\]\]", "", text).strip()
    if not text or text == "void":
        return None
    pieces = split_top_level(text, "=")
    declaration = pieces[0].strip()
    # A C++ default is not automatically a script default: the native wrapper
    # still expects its full positional arity unless binding metadata says
    # otherwise. Keep it out of the generated language contract.
    declaration = re.sub(r"\s*\.\.\.\s*$", "", declaration)
    name_match = re.search(r"([A-Za-z_]\w*)\s*(?:\[.*\])?$", declaration)
    if not name_match:
        return Parameter(f"arg{index}", declaration, script_type(declaration)[0])
    name = name_match.group(1)
    cpp_type = declaration[: name_match.start()].strip()
    if not cpp_type:
        return Parameter(f"arg{index}", declaration, script_type(declaration)[0])
    mapped, nullable = script_type(cpp_type)
    return Parameter(name, cpp_type, mapped, nullable)


def parse_parameters(text: str) -> list[Parameter]:
    result: list[Parameter] = []
    for index, item in enumerate(split_top_level(text)):
        parameter = parse_parameter(item, index)
        if parameter:
            result.append(parameter)
    return result


def class_blocks(source: str) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    masked = mask_comments(source)
    pattern = re.compile(r"\b(?:class|struct)\s+([A-Za-z_]\w*)[^;{]*\{")
    for match in pattern.finditer(masked):
        opening = masked.find("{", match.start())
        closing = matching(masked, opening, "{", "}")
        if closing is not None:
            result.setdefault(match.group(1), []).append(source[opening + 1 : closing])
    return result


class SignatureIndex:
    def __init__(self, sources: dict[Path, str]) -> None:
        self.sources = sources
        self.masked = {path: mask_comments(source) for path, source in sources.items()}
        self.classes: dict[str, list[str]] = {}
        self.aliases: dict[str, str] = {}
        for source in sources.values():
            for name, blocks in class_blocks(source).items():
                self.classes.setdefault(name, []).extend(blocks)
            for match in re.finditer(
                r"\busing\s+([A-Za-z_]\w*)\s*=\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\s*(?:<[^;]+>)?\s*;",
                mask_comments(source),
            ):
                self.aliases[match.group(1)] = match.group(2)
        for concrete_class in self.classes:
            self.aliases.pop(concrete_class, None)
        self.member_cache: dict[tuple[str, str], tuple[str, list[Parameter]] | None] = {}
        self.free_cache: dict[str, tuple[str, list[Parameter]] | None] = {}

    def member(self, qualified: str, method: str) -> tuple[str, list[Parameter]] | None:
        class_name = qualified.split("::")[-1]
        cache_key = (class_name, method)
        if cache_key in self.member_cache:
            return self.member_cache[cache_key]
        lookup_class = self.aliases.get(class_name, class_name)
        candidates: list[tuple[str, list[Parameter]]] = []
        declaration_pattern = re.compile(
            rf"(?m)(?:^|[;{{}}])\s*(?:virtual\s+|static\s+|inline\s+|constexpr\s+|explicit\s+)*"
            rf"([A-Za-z_~][\w:\s<>,*&]*?)\b{re.escape(method)}\s*\(")
        for block in self.classes.get(lookup_class, []):
            masked = mask_comments(block)
            for match in declaration_pattern.finditer(masked):
                opening = masked.find("(", match.start())
                closing = matching(block, opening)
                if closing is None:
                    continue
                candidates.append((match.group(1).strip().splitlines()[-1].strip(),
                                   parse_parameters(block[opening + 1 : closing])))
        if not candidates:
            definition_pattern = re.compile(
                rf"(?m)([A-Za-z_~][\w:\s<>,*&]*?)\b{re.escape(lookup_class)}::{re.escape(method)}\s*\(")
            needle = f"{lookup_class}::{method}"
            for path, source in self.sources.items():
                if needle not in source:
                    continue
                for match in definition_pattern.finditer(self.masked[path]):
                    opening = self.masked[path].find("(", match.start())
                    closing = matching(source, opening)
                    if closing is None:
                        continue
                    candidates.append((match.group(1).strip().splitlines()[-1].strip(),
                                       parse_parameters(source[opening + 1 : closing])))
        if not candidates and method == "getName":
            candidates.append(("std::string", []))
        if not candidates:
            inherited: list[tuple[str, list[Parameter]]] = []
            for blocks in self.classes.values():
                for block in blocks:
                    masked = mask_comments(block)
                    for match in declaration_pattern.finditer(masked):
                        opening = masked.find("(", match.start())
                        closing = matching(block, opening)
                        if closing is not None:
                            inherited.append((match.group(1).strip().splitlines()[-1].strip(),
                                              parse_parameters(block[opening + 1 : closing])))
            shapes = {
                (return_type, tuple((parameter.name, parameter.cpp_type) for parameter in parameters))
                for return_type, parameters in inherited
            }
            if len(shapes) == 1 and inherited:
                candidates.append(inherited[0])
        if not candidates:
            self.member_cache[cache_key] = None
            return None
        candidates.sort(key=lambda item: (sum(parameter.name.startswith("arg") for parameter in item[1]),
                                          -len(item[1])))
        self.member_cache[cache_key] = candidates[0]
        return candidates[0]

    def free(self, name: str) -> tuple[str, list[Parameter]] | None:
        if name in self.free_cache:
            return self.free_cache[name]
        pattern = re.compile(rf"(?m)^\s*(?:static\s+)?([A-Za-z_][\w:\s<>,*&]*?)\b{re.escape(name)}\s*\(")
        candidates = []
        needle = f"{name}("
        for path, source in self.sources.items():
            if needle not in source and f"{name} (" not in source:
                continue
            for match in pattern.finditer(self.masked[path]):
                opening = self.masked[path].find("(", match.start())
                closing = matching(source, opening)
                if closing is not None:
                    candidates.append((match.group(1).strip(), parse_parameters(source[opening + 1 : closing])))
        self.free_cache[name] = candidates[0] if candidates else None
        return self.free_cache[name]


def module_for(path: Path) -> str:
    relative = path.relative_to(ROOT).parts
    if len(relative) >= 3 and relative[:2] == ("src", "modules"):
        return relative[2]
    if len(relative) >= 3 and relative[:2] == ("src", "engine"):
        return relative[2]
    return "engine"


def class_bindings(source: str) -> list[tuple[int, str, str]]:
    pattern = re.compile(
        r"\b(?:auto|ssq::Class)\s+([A-Za-z_]\w*)\s*=\s*[A-Za-z_]\w*\.addClass"
        r"(?:<\s*([A-Za-z_:]\w*(?:::\w+)*)[^>]*>)?\s*\(")
    result = []
    for match in pattern.finditer(mask_comments(source)):
        opening = source.find("(", match.start())
        closing = matching(source, opening)
        if closing is None:
            continue
        arguments = split_top_level(source[opening + 1 : closing])
        literal = re.match(r'\s*"([^"]+)"', arguments[0]) if arguments else None
        result.append((match.start(), match.group(1), literal.group(1) if literal else (match.group(2) or "")))
    return result


def callable_signature(expression: str, signatures: SignatureIndex) -> tuple[str, str, list[Parameter]] | None:
    member = re.search(r"&\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)::([A-Za-z_]\w*)", expression)
    if member:
        signature = signatures.member(member.group(1), member.group(2))
        return (member.group(1).split("::")[-1], *signature) if signature else None

    lambda_match = re.search(r"\[[^]]*\]\s*\(", expression)
    if lambda_match:
        opening = expression.find("(", lambda_match.start())
        closing = matching(expression, opening)
        if closing is None:
            return None
        tail = expression[closing + 1 :]
        result = re.match(r"\s*(?:mutable\s*)?(?:noexcept\s*)?(?:->\s*([^\s{]+(?:\s*[*&])?))?", tail)
        return "", (result.group(1).strip() if result and result.group(1) else "dynamic"), parse_parameters(
            expression[opening + 1 : closing]
        )

    function = re.search(r"(?:std::function\s*<[^>]+>\s*\()?\s*([A-Za-z_]\w*)\s*\)?\s*$", expression)
    if function:
        signature = signatures.free(function.group(1))
        return ("", *signature) if signature else None
    return None


def extract_contracts(sources: dict[Path, str], enabled_modules: set[str] | None = None,
                      platform: str = "") -> tuple[list[Contract], list[str]]:
    signatures = SignatureIndex(sources)
    contracts: dict[str, Contract] = {}
    unresolved: list[str] = []
    call_pattern = re.compile(r"\b([A-Za-z_]\w*)\.addFunc\s*\(")
    for path, source in sources.items():
        relative = path.relative_to(ROOT).parts
        if (enabled_modules is not None and len(relative) >= 3 and relative[:2] == ("src", "modules")
                and relative[2] not in enabled_modules):
            continue
        bindings = class_bindings(source)
        masked = mask_comments(source)
        for match in call_pattern.finditer(masked):
            opening = source.find("(", match.start())
            closing = matching(source, opening)
            if closing is None:
                unresolved.append(f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: unclosed addFunc")
                continue
            arguments = split_top_level(source[opening + 1 : closing])
            method_match = re.match(r'\s*"([^"]+)"', arguments[0]) if arguments else None
            if not method_match or len(arguments) < 2:
                unresolved.append(f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: dynamic addFunc")
                continue
            method = method_match.group(1)
            expression = ",".join(arguments[1:])
            signature = callable_signature(expression, signatures)
            if signature is None:
                unresolved.append(
                    f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: {match.group(1)}.{method}"
                )
                continue
            cpp_class, return_cpp, parameters = signature
            script_class = ""
            for position, receiver, bound_class in reversed(bindings):
                if position < match.start() and receiver == match.group(1):
                    script_class = bound_class
                    break
            if not script_class:
                script_class = cpp_class if match.group(1) not in {"table", "eve", "root", "vm"} else ""
            if (parameters and cpp_class == "" and "*" in parameters[0].cpp_type
                    and match.group(1) not in {"table", "eve", "root", "vm"}):
                parameters = parameters[1:]
            mapped_return, return_nullable = script_type(return_cpp)
            ownership = "borrowed" if "*" in return_cpp else "value"
            module = module_for(path)
            contract = Contract(module, script_class, method, parameters, mapped_return, return_nullable, ownership,
                                "main", [platform] if platform else [],
                                f"{module}.{script_class + '.' if script_class else ''}{method}",
                                f"{path.relative_to(ROOT).as_posix()}:{source.count(chr(10), 0, match.start()) + 1}")
            existing = contracts.get(contract.key)
            if existing is None or sum(p.name.startswith("arg") for p in contract.parameters) < sum(
                p.name.startswith("arg") for p in existing.parameters
            ):
                contracts[contract.key] = contract
    return sorted(contracts.values(), key=lambda item: item.key), unresolved


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def write_cpp(path: Path, contracts: list[Contract]) -> None:
    lines = [
        '// Generated by scripts/generate_binding_contracts.py; do not edit.',
        '#include "common/BindingContracts.h"',
        '#include "common/ScriptCompiler.h"',
        '',
        '#include <initializer_list>',
        '',
        'namespace eve::script {',
        'namespace {',
        'struct ParameterSeed {',
        '    const char* name;',
        '    const char* type;',
        '    bool nullable;',
        '    const char* scriptDefault;',
        '    ScriptUnit unit;',
        '    std::initializer_list<const char*> choices;',
        '};',
        '',
        'void addContract(BindingContractRegistry& registry, const char* module, const char* scriptClass,',
        '                 const char* method, const char* returnType, bool returnNullable, const char* ownership,',
        '                 const char* threadAffinity, std::initializer_list<const char*> platforms,',
        '                 const char* documentationId, std::initializer_list<ParameterSeed> parameters) {',
        '    BindingContract contract;',
        '    contract.module = module;',
        '    contract.scriptClass = scriptClass;',
        '    contract.method = method;',
        '    contract.returnType = returnType;',
        '    contract.returnNullable = returnNullable;',
        '    contract.ownership = ownership;',
        '    contract.threadAffinity = threadAffinity;',
        '    for (const char* platform : platforms) contract.platforms.emplace_back(platform);',
        '    contract.documentationId = documentationId;',
        '    for (const ParameterSeed& seed : parameters) {',
        '        BindingParameterContract parameter;',
        '        parameter.name = seed.name;',
        '        parameter.type = seed.type;',
        '        parameter.nullable = seed.nullable;',
        '        if (seed.scriptDefault != nullptr) parameter.scriptDefault = seed.scriptDefault;',
        '        parameter.unit = seed.unit;',
        '        for (const char* choice : seed.choices) parameter.choices.emplace_back(choice);',
        '        contract.parameters.push_back(std::move(parameter));',
        '    }',
        '    registry.registerContract(std::move(contract));',
        '}',
    ]
    calls: list[str] = []
    for contract in contracts:
        parameters = []
        for parameter in contract.parameters:
            default = cpp_string(parameter.default) if parameter.default else 'nullptr'
            choices = "{" + ", ".join(cpp_string(choice) for choice in parameter.choices) + "}"
            parameters.append(
                f'{{{cpp_string(parameter.name)}, {cpp_string(parameter.script_type)}, '
                f'{str(parameter.nullable).lower()}, {default}, ScriptUnit::{parameter.unit}, {choices}}}'
            )
        calls.append(
            f'    addContract(registry, {cpp_string(contract.module)}, {cpp_string(contract.script_class)}, '
            f'{cpp_string(contract.method)}, {cpp_string(contract.return_type)}, '
            f'{str(contract.return_nullable).lower()}, {cpp_string(contract.ownership)}, '
            f'{cpp_string(contract.thread_affinity)}, '
            f'{{{", ".join(cpp_string(platform) for platform in contract.platforms)}}}, '
            f'{cpp_string(contract.documentation_id)}, {{{", ".join(parameters)}}});'
        )
    chunk_size = 128
    chunk_count = (len(calls) + chunk_size - 1) // chunk_size
    for index in range(chunk_count):
        lines += [
            '',
            '#if defined(_MSC_VER)',
            '__declspec(noinline)',
            '#elif defined(__GNUC__) || defined(__clang__)',
            '__attribute__((noinline))',
            '#endif',
            f'void registerChunk{index}(BindingContractRegistry& registry) {{',
            *calls[index * chunk_size : (index + 1) * chunk_size],
            '}',
        ]
    lines += ['}  // namespace', '', 'void registerEngineBindingContracts(BindingContractRegistry& registry) {']
    for index in range(chunk_count):
        lines.append(f'    registerChunk{index}(registry);')
    lines += ['}', '', '}  // namespace eve::script', '']
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def source_files() -> dict[Path, str]:
    result = {}
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.suffix in SOURCE_SUFFIXES and ".generated." not in path.name:
                result[path] = path.read_text(encoding="utf-8", errors="replace")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--allow-unresolved", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--modules", help="Comma-separated enabled src/modules directories; engine bindings are always included")
    parser.add_argument("--platform", default="", help="Platform availability recorded on generated contracts")
    args = parser.parse_args()

    enabled_modules = ({item for item in args.modules.split(",") if item} if args.modules is not None else None)
    contracts, unresolved = extract_contracts(source_files(), enabled_modules, args.platform)
    placeholder_count = sum(parameter.name.startswith("arg") for contract in contracts for parameter in contract.parameters)
    print(f"Binding Contract: {len(contracts)} contracts, {len(unresolved)} unresolved bindings, "
          f"{placeholder_count} placeholder parameter names")
    placeholders = [
        f"{contract.source}: {contract.key}: " + ", ".join(
            f"{parameter.name}<{parameter.cpp_type}>" for parameter in contract.parameters if parameter.name.startswith("arg")
        )
        for contract in contracts
        if any(parameter.name.startswith("arg") for parameter in contract.parameters)
    ]
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        report = [*(f"UNRESOLVED {item}" for item in unresolved), *(f"PLACEHOLDER {item}" for item in placeholders)]
        args.report.write_text("\n".join(report) + ("\n" if report else ""), encoding="utf-8")
    if args.output:
        write_cpp(args.output, contracts)
    if (unresolved or placeholder_count) and not args.allow_unresolved:
        for item in unresolved[:50]:
            print(f"UNRESOLVED {item}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
