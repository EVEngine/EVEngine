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
import hashlib
from dataclasses import dataclass, field, replace
import json
from pathlib import Path
import re
import shlex
import sys
import utf8_stdio
from binding_architecture import attach_architecture
from binding_semantics import attach_semantics
from check_module_manifest import (
    DECLARATION_START,
    DIR_RE,
    MANIFEST,
    NAME_RE,
    _balanced_block,
    default_module_dir,
    manifest_files,
)


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = (ROOT / "src" / "engine", ROOT / "src" / "modules")
SOURCE_SUFFIXES = {".h", ".hpp", ".cpp"}
_MANIFEST_KEYWORDS = {
    "NAME",
    "LIB",
    "LAYER",
    "DIR",
    "DEPS",
    "OPTIONAL_DEPS",
    "THIRDPARTY",
    "SCRIPT",
    "SLOT",
    "GROUP",
    "REQUIRED",
    "CORE",
    "HEADER_ONLY",
}

# C++ attributes such as [[nodiscard]] / [[nodiscard("...")]] may precede a
# declaration. Skip them before matching the return type so member bindings to
# attributed getters stay resolvable. Attribute bodies may contain '(' (message
# arguments), so callers must take the function '(' from the end of the regex
# match rather than the first '(' after match.start().
CXX_ATTRIBUTE = r"(?:\[\[[^\]]*\]\]\s*)*"
CXX_DECL_PREFIX = (
    r"(?:virtual\s+|static\s+|inline\s+|constexpr\s+|explicit\s+|"
    rf"{CXX_ATTRIBUTE})*"
)


def declaration_opening_paren(match: re.Match[str]) -> int:
    """Return the index of the function '(' that ends a declaration regex match."""
    return match.end() - 1


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
    cpp_symbol: str = ""
    architecture: dict | None = None
    semantics: dict | None = None

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


def class_blocks(source: str, masked: str | None = None) -> dict[str, list[str]]:
    result: dict[str, list[str]] = {}
    masked = mask_comments(source) if masked is None else masked
    # The engine's per-link-group export macro (src/engine/common/Export.h) sits
    # between the class key and the class name -- `class EVENGINE_API_DOMAINS
    # Widget {` -- so the token after `class`/`struct` is not always the name.
    # Without this the class is registered under the macro's name and every
    # member binding of it is reported as unresolved.
    pattern = re.compile(
        r"\b(?:class|struct)\s+(?:EVENGINE_API\w*\s+)?([A-Za-z_]\w*)[^;{]*\{"
    )
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
        for path, source in sources.items():
            for name, blocks in class_blocks(source, self.masked[path]).items():
                self.classes.setdefault(name, []).extend(blocks)
            for match in re.finditer(
                r"\busing\s+([A-Za-z_]\w*)\s*=\s*(?:[A-Za-z_]\w*::)*([A-Za-z_]\w*)\s*(?:<[^;]+>)?\s*;",
                self.masked[path],
            ):
                self.aliases[match.group(1)] = match.group(2)
        for concrete_class in self.classes:
            self.aliases.pop(concrete_class, None)
        self.masked_blocks = {block: mask_comments(block)
                              for blocks in self.classes.values() for block in blocks}
        # Index identifiers once: unresolved/inherited methods must not rescan
        # every class body for every binding. Preserve original block order.
        self.method_blocks: dict[str, list[str]] = {}
        for blocks in self.classes.values():
            for block in blocks:
                for name in set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", self.masked_blocks[block])):
                    self.method_blocks.setdefault(name, []).append(block)
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
            rf"(?m)(?:^|[;{{}}])\s*{CXX_DECL_PREFIX}"
            rf"([A-Za-z_~][\w:\s<>,*&]*?)\b{re.escape(method)}\s*\(")
        for block in self.classes.get(lookup_class, []):
            masked = self.masked_blocks[block]
            for match in declaration_pattern.finditer(masked):
                opening = declaration_opening_paren(match)
                closing = matching(block, opening)
                if closing is None:
                    continue
                candidates.append((match.group(1).strip().splitlines()[-1].strip(),
                                   parse_parameters(block[opening + 1 : closing])))
        if not candidates:
            definition_pattern = re.compile(
                rf"(?m){CXX_DECL_PREFIX}"
                rf"([A-Za-z_~][\w:\s<>,*&]*?)\b{re.escape(lookup_class)}::{re.escape(method)}\s*\(")
            needle = f"{lookup_class}::{method}"
            for path, source in self.sources.items():
                if needle not in source:
                    continue
                for match in definition_pattern.finditer(self.masked[path]):
                    opening = declaration_opening_paren(match)
                    closing = matching(source, opening)
                    if closing is None:
                        continue
                    candidates.append((match.group(1).strip().splitlines()[-1].strip(),
                                       parse_parameters(source[opening + 1 : closing])))
        if not candidates and method == "getName":
            candidates.append(("std::string", []))
        if not candidates:
            inherited: list[tuple[str, list[Parameter]]] = []
            for block in self.method_blocks.get(method, []):
                masked = self.masked_blocks[block]
                for match in declaration_pattern.finditer(masked):
                    opening = declaration_opening_paren(match)
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
        pattern = re.compile(
            rf"(?m)^\s*(?:static\s+|{CXX_ATTRIBUTE})*"
            rf"([A-Za-z_][\w:\s<>,*&]*?)\b{re.escape(name)}\s*\(")
        candidates = []
        needle = f"{name}("
        for path, source in self.sources.items():
            if needle not in source and f"{name} (" not in source:
                continue
            for match in pattern.finditer(self.masked[path]):
                opening = declaration_opening_paren(match)
                closing = matching(source, opening)
                if closing is not None:
                    candidates.append((match.group(1).strip(), parse_parameters(source[opening + 1 : closing])))
        candidates.sort(key=lambda item: (sum(parameter.name.startswith("arg") for parameter in item[1]),
                                          -len(item[1]), item[0],
                                          tuple((parameter.name, parameter.cpp_type) for parameter in item[1])))
        self.free_cache[name] = candidates[0] if candidates else None
        return self.free_cache[name]


def module_for(path: Path) -> str:
    relative = path.relative_to(ROOT).parts
    if len(relative) >= 3 and relative[:2] == ("src", "modules"):
        return relative[2]
    if len(relative) >= 3 and relative[:2] == ("src", "engine"):
        return relative[2]
    return "engine"


@dataclass(frozen=True)
class SlotIndex:
    """SCRIPT→SLOT pairs and single-SLOT directory prefixes from the module manifest.

    Module_REG bindings use ``addClass(name, …)`` so the generator cannot see the
    runtime root slot in source. Contract keys must still match that slot
    (``dialogueFlow/publishGameplay``, ``levelEditor/newTileBuffer``).
    """

    script_to_slot: dict[str, str]
    dir_slots: tuple[tuple[str, str], ...]


def load_slot_index(manifest: Path = MANIFEST) -> SlotIndex:
    script_to_slot: dict[str, str] = {}
    dir_slots: list[tuple[str, str]] = []
    for manifest_file in manifest_files(manifest):
        lines = manifest_file.read_text(encoding="utf-8").splitlines(keepends=True)
        index = 0
        while index < len(lines):
            if not DECLARATION_START.match(lines[index]):
                index += 1
                continue
            block, end = _balanced_block(lines, index)
            index = end + 1
            name_match = NAME_RE.search(block)
            if not name_match:
                continue
            declared_dir = DIR_RE.search(block)
            directory = declared_dir.group(1) if declared_dir else default_module_dir(name_match.group(1))
            tokens = shlex.split(block.replace("\n", " "))
            if tokens and tokens[0].startswith("eve_declare_module("):
                tokens[0] = tokens[0][len("eve_declare_module(") :]
                if not tokens[0]:
                    tokens.pop(0)
            scripts: list[str] = []
            slots: list[str] = []
            cursor = 0
            while cursor < len(tokens):
                key = tokens[cursor].rstrip(")")
                cursor += 1
                if key in {"REQUIRED", "CORE", "HEADER_ONLY"}:
                    continue
                if key in {"NAME", "LIB", "LAYER", "DIR"}:
                    if cursor < len(tokens):
                        cursor += 1
                    continue
                if key in {"DEPS", "OPTIONAL_DEPS", "THIRDPARTY", "GROUP"}:
                    while cursor < len(tokens) and tokens[cursor].rstrip(")") not in _MANIFEST_KEYWORDS:
                        cursor += 1
                    continue
                if key == "SCRIPT":
                    while cursor < len(tokens) and tokens[cursor].rstrip(")") not in _MANIFEST_KEYWORDS:
                        scripts.append(tokens[cursor].rstrip(")"))
                        cursor += 1
                    continue
                if key == "SLOT":
                    while cursor < len(tokens) and tokens[cursor].rstrip(")") not in _MANIFEST_KEYWORDS:
                        slots.append(tokens[cursor].rstrip(")"))
                        cursor += 1
                    continue
                # Unknown token: skip one value if present so a new flag cannot abort the scan.
                if cursor < len(tokens) and tokens[cursor].rstrip(")") not in _MANIFEST_KEYWORDS:
                    cursor += 1
            for script_name, slot in zip(scripts, slots):
                if script_name and slot:
                    script_to_slot[script_name] = slot
            # Only nested package DIRs (map/level/editor → levelEditor). Host roots such as
            # animation/graphics keep their directory module id in contract keys even when the
            # runtime SLOT spelling differs (anim/gfx); SCRIPT→SLOT covers Module_REG methods.
            normalized_dir = directory.replace("\\", "/").strip("/")
            if len(slots) == 1 and slots[0] and "/" in normalized_dir:
                dir_slots.append((normalized_dir, slots[0]))
    dir_slots.sort(key=lambda item: len(item[0]), reverse=True)
    return SlotIndex(script_to_slot=script_to_slot, dir_slots=tuple(dir_slots))


def resolve_slot_module(path: Path, host_module: str, cpp_class: str, script_class: str,
                        index: SlotIndex) -> str:
    """Prefer the manifest SLOT over the host package directory when known.

    Module_REG roots bind with an empty ``scriptClass``; their contract module must
    be the runtime SLOT (``dialogueFlow``, ``gfx``, ``levelEditor``). Named script
    classes keep the host package id (``graphics/AlphaMask.*``) unless a nested
    package DIR maps the file.
    """

    if not script_class:
        if cpp_class and cpp_class in index.script_to_slot:
            return index.script_to_slot[cpp_class]
    elif script_class in index.script_to_slot:
        return index.script_to_slot[script_class]
    try:
        relative = path.relative_to(ROOT / "src" / "modules").as_posix()
    except ValueError:
        return host_module
    for directory, slot in index.dir_slots:
        if relative == directory or relative.startswith(directory + "/"):
            return slot
    return host_module


def class_bindings(source: str, masked: str | None = None) -> list[tuple[int, str, str]]:
    masked = mask_comments(source) if masked is None else masked
    pattern = re.compile(
        r"\b(?:auto|ssq::Class)\s+([A-Za-z_]\w*)\s*=\s*[A-Za-z_]\w*\.addClass"
        r"(?:<\s*([A-Za-z_:]\w*(?:::\w+)*)[^>]*>)?\s*\(")
    result = []
    for match in pattern.finditer(masked):
        opening = source.find("(", match.start())
        closing = matching(source, opening)
        if closing is None:
            continue
        arguments = split_top_level(source[opening + 1 : closing])
        literal = re.match(r'\s*"([^"]+)"', arguments[0]) if arguments else None
        # Do not infer scriptClass from Type::create: that rewrites contract keys
        # (module/Class.method) and invalidates reviewed semantics maps.
        result.append((match.start(), match.group(1), literal.group(1) if literal else (match.group(2) or "")))
    helper_pattern = re.compile(r'\bauto\s+(\w+)\s*=\s*(?:editor::)?addScriptClass\s*<[^>]+>\s*\(\s*\w+\s*,\s*"([^"\n]+)"')
    for match in helper_pattern.finditer(masked):
        result.append((match.start(), match.group(1), match.group(2)))
    return sorted(result)


def callable_signature(expression: str, signatures: SignatureIndex) -> tuple[str, str, list[Parameter]] | None:
    member = re.search(r"&\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)::([A-Za-z_]\w*)", expression)
    if member:
        signature = signatures.member(member.group(1), member.group(2))
        return (member.group(1).split("::")[-1], *signature) if signature else None

    # Capture lists may contain calls (`[vm = cls.getHandle()](...)`). The parameter
    # '(' is the one that ends the `]\s*(` match — never the first '(' in the capture.
    lambda_match = re.search(r"\[[^\]]*\]\s*\(", expression)
    if lambda_match:
        opening = lambda_match.end() - 1
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
                      platform: str = "", slot_index: SlotIndex | None = None) -> tuple[list[Contract], list[str]]:
    signatures = SignatureIndex(sources)
    contracts: dict[str, Contract] = {}
    unresolved: list[str] = []
    helper_contracts: dict[str, list[Contract]] = {}
    helper_pattern = re.compile(
        r"template\s*<[^>]+>\s*void\s+(\w+)\s*\(\s*ssq::Class\s*&\s*(\w+)"
    )
    slots = slot_index if slot_index is not None else load_slot_index()
    class_aliases: dict[str, set[str]] = {}
    for path, source in sources.items():
        relative = path.relative_to(ROOT).parts
        if (enabled_modules is not None and relative[:2] == ("src", "modules")
                and relative[2] not in enabled_modules):
            continue
        for position, _, script_class in class_bindings(source):
            declaration = source[position:source.find("(", position)]
            cpp_class = re.search(r"\.addClass\s*<\s*([^,>]+)", declaration)
            if cpp_class:
                mapped, _ = script_type(cpp_class.group(1))
                if mapped not in {"dynamic", "int", "float", "bool", "string", "void",
                                  "Array<dynamic>", "Table<string, dynamic>"} and script_class:
                    class_aliases.setdefault(mapped, set()).add(script_class)
    aliases = {cpp: next(iter(names)) for cpp, names in class_aliases.items() if len(names) == 1}
    # SimpleSquirrel addFunc plus the table-driven helpers in SquirrelBindContext.h.
    # Helpers must be qualified (`script::bindMethod`) so the helper definitions themselves
    # are not scraped. Method names stay string literals in argument 1.
    call_pattern = re.compile(
        r"(?:(?:eve::)?script::(?P<helper>bindMethod|bindPointerMethod|bindNullSafe)\s*\()|"
        r"(?:(?P<receiver>[A-Za-z_]\w*)\.addFunc\s*\()"
    )
    bindings_by_path: dict[Path, list[tuple[int, str, str]]] = {}
    for path, source in sources.items():
        relative = path.relative_to(ROOT).parts
        if (enabled_modules is not None and len(relative) >= 3 and relative[:2] == ("src", "modules")
                and relative[2] not in enabled_modules):
            continue
        masked = signatures.masked[path]
        bindings = class_bindings(source, masked)
        bindings_by_path[path] = bindings
        helpers = []
        for helper in helper_pattern.finditer(masked):
            opening = masked.find("{", helper.end())
            closing = matching(source, opening, "{", "}") if opening >= 0 else None
            if closing is not None:
                helpers.append((opening, closing, helper.group(1), helper.group(2)))
        for match in call_pattern.finditer(masked):
            opening = source.find("(", match.start())
            closing = matching(source, opening)
            helper = match.group("helper")
            receiver = match.group("receiver") or ""
            label = helper or f"{receiver}.addFunc"
            if closing is None:
                unresolved.append(
                    f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: unclosed {label}"
                )
                continue
            arguments = split_top_level(source[opening + 1 : closing])
            if helper:
                # bindMethod(cls, "name", expr) / bindNullSafe(cls, "name", getter, whenNull)
                if len(arguments) < 3:
                    unresolved.append(
                        f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: dynamic {helper}"
                    )
                    continue
                method_match = re.match(r'\s*"([^"]+)"', arguments[1])
                expression = ",".join(arguments[2:3] if helper == "bindNullSafe" else arguments[2:])
                receiver_expr = arguments[0].strip()
            else:
                method_match = re.match(r'\s*"([^"]+)"', arguments[0]) if arguments else None
                if not method_match or len(arguments) < 2:
                    unresolved.append(
                        f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: dynamic addFunc"
                    )
                    continue
                expression = ",".join(arguments[1:])
                receiver_expr = receiver
            if not method_match:
                unresolved.append(
                    f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: dynamic {label}"
                )
                continue
            method = method_match.group(1)
            signature = callable_signature(expression, signatures)
            if signature is None:
                unresolved.append(
                    f"{path.relative_to(ROOT)}:{source.count(chr(10), 0, match.start()) + 1}: {label}.{method}"
                )
                continue
            cpp_class, return_cpp, parameters = signature
            script_class = ""
            for position, bound_receiver, bound_class in reversed(bindings):
                if position < match.start() and bound_receiver == receiver_expr:
                    script_class = bound_class
                    break
            if not script_class:
                script_class = cpp_class if receiver_expr not in {"table", "eve", "root", "vm", "cls"} else ""
            # Lambdas on Module_REG roots omit a C++ class in the signature scrape
            # (`return "", …`). Recover `T` from the leading `T* self` only when the
            # addFunc receiver is the class variable (`cls` / `module`), not a free
            # table helper whose first argument may be an unrelated pointer.
            slot_owner = cpp_class
            if (not slot_owner and receiver_expr in {"cls", "module"} and parameters
                    and "*" in parameters[0].cpp_type):
                pointed = re.sub(r"\bconst\b", "", parameters[0].cpp_type)
                pointed = pointed.replace("*", " ").replace("&", " ").strip().split("::")[-1]
                pointed = pointed.split()[-1] if pointed.split() else ""
                if pointed:
                    slot_owner = pointed
            if (parameters and cpp_class == "" and "*" in parameters[0].cpp_type
                    and receiver_expr not in {"table", "eve", "root", "vm"}):
                parameters = parameters[1:]
            mapped_return, return_nullable = script_type(return_cpp)
            mapped_return = aliases.get(mapped_return, mapped_return)
            parameters = [replace(parameter, script_type=aliases.get(parameter.script_type, parameter.script_type))
                          for parameter in parameters]
            ownership = "borrowed" if "*" in return_cpp else "value"
            host_module = module_for(path)
            module = resolve_slot_module(path, host_module, slot_owner, script_class, slots)
            contract = Contract(module, script_class, method, parameters, mapped_return, return_nullable, ownership,
                                "main", [platform] if platform else [],
                                f"{module}.{script_class + '.' if script_class else ''}{method}",
                                f"{path.relative_to(ROOT).as_posix()}:{source.count(chr(10), 0, match.start()) + 1}")
            helper_name = next((name for start, end, name, receiver in helpers
                                if start < match.start() < end and receiver == receiver_expr), None)
            if helper_name:
                helper_contracts.setdefault(helper_name, []).append(contract)
                continue
            existing = contracts.get(contract.key)
            direct_member = re.fullmatch(r"\s*&\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)+)\s*", expression)
            if direct_member:
                contract.cpp_symbol = direct_member.group(1)
            if existing is None or sum(p.name.startswith("arg") for p in contract.parameters) < sum(
                p.name.startswith("arg") for p in existing.parameters
            ):
                contracts[contract.key] = contract
    # Expand shared template binders at actual class-registration call sites.
    # The methods still come from addFunc declarations, so no duplicate API list
    # or per-domain registration wrapper is needed for discovery/LSP.
    for name, methods in helper_contracts.items():
        pattern = re.compile(r"\b" + re.escape(name) + r"\s*(?:<[^{}();]+>)?\s*\(\s*(\w+)\s*[,)]")
        for path, source in sources.items():
            relative = path.relative_to(ROOT).parts
            if (enabled_modules is not None and len(relative) >= 3 and relative[:2] == ("src", "modules")
                    and relative[2] not in enabled_modules):
                continue
            masked = signatures.masked[path]
            bindings = bindings_by_path[path]
            for call in pattern.finditer(masked):
                bound_class = next((bound for position, receiver, bound in reversed(bindings)
                                    if position < call.start() and receiver == call.group(1)), "")
                if not bound_class:
                    continue
                module = resolve_slot_module(path, module_for(path), "", bound_class, slots)
                for method in methods:
                    contract = replace(method, module=module, script_class=bound_class,
                                       documentation_id=f"{module}.{bound_class}.{method.method}")
                    contracts[contract.key] = contract
    return sorted(contracts.values(), key=lambda item: item.key), unresolved


def cpp_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def contract_to_dict(contract: Contract) -> dict:
    parameters = []
    for parameter in contract.parameters:
        item: dict = {
            "name": parameter.name,
            "type": parameter.script_type,
            "nullable": parameter.nullable,
        }
        if parameter.default:
            item["default"] = parameter.default
        if parameter.unit != "None":
            item["unit"] = parameter.unit
        if parameter.choices:
            item["choices"] = parameter.choices
        parameters.append(item)
    return {
        "key": contract.key,
        "module": contract.module,
        "scriptClass": contract.script_class,
        "method": contract.method,
        "parameters": parameters,
        "returnType": contract.return_type,
        "returnNullable": contract.return_nullable,
        "ownership": contract.ownership,
        "threadAffinity": contract.thread_affinity,
        "platforms": contract.platforms,
        "documentationId": contract.documentation_id,
        "source": contract.source,
        "cppSymbol": contract.cpp_symbol,
        "semantics": contract.semantics,
        "architecture": contract.architecture or {
            "status": "unknown", "reason": "No reviewed owner mapping for this binding"
        },
    }


def write_json(path: Path, contracts: list[Contract]) -> None:
    payload = {
        "schema": "eve.binding-api",
        "schemaVersion": 1,
        "count": len(contracts),
        "contracts": [contract_to_dict(contract) for contract in contracts],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def dts_type(script_type: str, nullable: bool) -> str:
    mapped = script_type if script_type else "any"
    return f"{mapped} | null" if nullable else mapped


def write_dts(path: Path, contracts: list[Contract]) -> None:
    grouped: dict[str, list[Contract]] = {}
    for contract in contracts:
        grouped.setdefault(contract.script_class or contract.module or "eve", []).append(contract)
    lines = [
        "/** Generated EveScript Binding Contracts as TypeScript-shaped signatures.",
        " *  Not a second runtime language. Agents may use this for recall; the engine still runs .nut.",
        " */",
        "",
    ]
    for owner in sorted(grouped):
        lines.append(f"export interface {owner} {{")
        for contract in grouped[owner]:
            params = ", ".join(
                f"{parameter.name}: {dts_type(parameter.script_type, parameter.nullable)}"
                for parameter in contract.parameters
            )
            returned = dts_type(contract.return_type, contract.return_nullable)
            lines.append(f"  {contract.method}({params}): {returned};")
        lines.append("}")
        lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


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
        '                 const char* documentationId, const char* source, const char* cppSymbol,',
        '                 std::optional<BindingArchitectureContract> architecture,',
        '                 std::optional<BindingSemanticContract> semantics,',
        '                 std::initializer_list<ParameterSeed> parameters) {',
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
        '    contract.source = source;',
        '    contract.cppSymbol = cppSymbol;',
        '    contract.architecture = std::move(architecture);',
        '    contract.semantics = std::move(semantics);',
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
        architecture = 'std::nullopt'
        semantics = 'std::nullopt'
        if contract.semantics:
            values = [cpp_string(contract.semantics[key]) for key in ('factory', 'role', 'resultClass')]
            values += [str(contract.semantics[key]) for key in ('identityArgument', 'relatedArgument')]
            values += [cpp_string(json.dumps(contract.semantics['schema'], ensure_ascii=False, separators=(',', ':')))
                       if contract.semantics.get('schema') else cpp_string('')]
            semantics = 'BindingSemanticContract{' + ', '.join(values) + '}'
        if contract.architecture:
            mapping = contract.architecture
            values = [cpp_string(mapping[key]) for key in (
                'implementationSource', 'stateOwner', 'ownerSource', 'inputHash', 'limitations'
            )]
            values += ['{' + ', '.join(cpp_string(value) for value in mapping[key]) + '}'
                       for key in ('writes', 'tests', 'reads')]
            architecture = 'BindingArchitectureContract{' + ', '.join(values) + '}'
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
            f'{cpp_string(contract.documentation_id)}, {cpp_string(contract.source)}, '
            f'{cpp_string(contract.cpp_symbol)}, {architecture}, {semantics}, {{{", ".join(parameters)}}});'
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
    provider_hash = hashlib.sha256(b'eve-script-structure-v1')
    for name in ('scripts/binding_semantics.py', 'src/engine/common/ScriptStructure.hpp',
                 'src/engine/common/ScriptStructure.cpp', 'src/engine/devtools/ProjectIndexAutomatic.cpp',
                 'src/engine/devtools/ProjectIndexGraph.cpp', 'src/engine/devtools/ProjectIndexScan.cpp',
                 'src/engine/devtools/McpProjectIndexTools.cpp'):
        source_path = ROOT / name
        if source_path.exists():
            provider_hash.update(name.encode())
            provider_hash.update(source_path.read_text(encoding='utf-8').replace('\r\n', '\n').encode())
    lines += ['}  // namespace', '',
              f'std::string_view scriptStructureProviderHash() {{ return "{provider_hash.hexdigest()}"; }}', '',
              'void registerEngineBindingContracts(BindingContractRegistry& registry) {']
    if chunk_count:
        # Binaryen can inline LLVM's noinline chunks again at link time. Keep
        # startup dispatch indirect, and prevent Clang from first unrolling it
        # back into direct calls (which creates a huge local-coalescing problem).
        lines += ['    using RegisterChunk = void (*)(BindingContractRegistry&);',
                  '    static constexpr RegisterChunk chunks[] = {']
        for index in range(chunk_count):
            lines.append(f'        registerChunk{index},')
        lines += ['    };',
                  '#if defined(__EMSCRIPTEN__)',
                  '#pragma clang loop unroll(disable)',
                  '#endif',
                  '    for (auto chunk : chunks) chunk(registry);']
    lines += ['}', '', '}  // namespace eve::script', '']
    path.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines)
    if path.exists() and path.read_text(encoding="utf-8") == text:
        return
    path.write_text(text, encoding="utf-8")


def source_files() -> dict[Path, str]:
    result = {}
    for root in SOURCE_ROOTS:
        for path in root.rglob("*"):
            if path.suffix not in SOURCE_SUFFIXES or ".generated." in path.name:
                continue
            # Helper templates only — their addFunc(name, ...) bodies are not contracts.
            if path.name == "SquirrelBindContext.h":
                continue
            result[path] = path.read_text(encoding="utf-8", errors="replace")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--json-output", type=Path, help="Write eve.binding-api JSON catalog for agents")
    parser.add_argument("--dts-output", type=Path, help="Write TypeScript-shaped signatures (not a second language)")
    parser.add_argument("--allow-unresolved", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--modules", help="Comma-separated enabled src/modules directories; engine bindings are always included")
    parser.add_argument("--platform", default="", help="Platform availability recorded on generated contracts")
    args = parser.parse_args()

    enabled_modules = ({item for item in args.modules.split(",") if item} if args.modules is not None else None)
    sources = source_files()
    contracts, unresolved = extract_contracts(sources, enabled_modules, args.platform)
    try:
        metadata_files = sorted((ROOT / "src/modules").glob("*/binding_architecture.json"))
        attach_architecture(contracts, sources, ROOT, metadata_files,
                            mask_comments, enabled_modules)
        attach_semantics(contracts, sources, ROOT,
                         sorted((ROOT / "src/modules").glob("*/binding_semantics.json")), mask_comments)
    except (ValueError, KeyError, OSError) as error:
        print(f"Architecture mapping error: {error}", file=sys.stderr)
        return 1
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
    if args.json_output:
        write_json(args.json_output, contracts)
    if args.dts_output:
        write_dts(args.dts_output, contracts)
    if (unresolved or placeholder_count) and not args.allow_unresolved:
        for item in unresolved[:50]:
            print(f"UNRESOLVED {item}")
        for item in placeholders[:50]:
            print(f"PLACEHOLDER {item}")
        return 1
    return 0


if __name__ == "__main__":
    utf8_stdio.enable_utf8_stdio()
    sys.exit(main())
