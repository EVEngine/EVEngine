"""Validate reviewed owner metadata against actual native binding evidence.

This is a bounded source mapping, not whole-program analysis. Signatures and
aliases come from the binding extractor; metadata only supplies domain facts.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
from collections.abc import Callable


def attach_architecture(contracts, sources: dict[Path, str], root: Path,
                        metadata_files: list[Path], mask_source: Callable[[str], str],
                        enabled_modules: set[str] | None = None) -> None:
    """Enrich enabled contracts; reject broken metadata even in trimmed builds."""
    entries = {}
    for metadata_path in sorted(metadata_files):
        payload = json.loads(metadata_path.read_text(encoding="utf-8"))
        if payload.get("schema") != "eve.binding-architecture-metadata" or payload.get("schemaVersion") != 1:
            raise ValueError(f"Unsupported architecture metadata: {metadata_path}")
        for entry in payload["entries"]:
            symbol = entry["symbol"]
            identity = (entry["module"], symbol)
            if identity in entries:
                raise ValueError(f"Duplicate architecture symbol: {symbol}")
            evidence = {metadata_path: metadata_path.read_text(encoding="utf-8")}

            def read(relative: str) -> tuple[Path, str]:
                path = (root / relative).resolve()
                if not path.is_relative_to(root.resolve()):
                    raise ValueError(f"Evidence escapes repository: {relative}")
                text = sources.get(path)
                if text is None:
                    text = path.read_text(encoding="utf-8")
                evidence[path] = text
                return path, text

            _, implementation = read(entry["implementationFile"])
            # A direct definition must exist; a declaration/comment is not evidence.
            pattern = re.compile(r"\b" + re.escape(symbol) + r"\s*\([^;{}]*\)\s*(?:const\s*)?\{")
            definitions = list(pattern.finditer(mask_source(implementation)))
            if len(definitions) != 1:
                raise ValueError(f"Expected one implementation for {symbol}")
            _, owner = read(entry["ownerFile"])
            for anchor in (entry["ownerAnchor"], entry["stateAnchor"]):
                if anchor not in mask_source(owner):
                    raise ValueError(f"Missing owner/state anchor for {symbol}: {anchor}")
            tests = []
            for test in entry["tests"]:
                _, text = read(test["file"])
                if not re.search(r'\bTEST_CASE(?:_FIXTURE)?\s*\([^)]*"' + re.escape(test["name"]) + r'"', mask_source(text)):
                    raise ValueError(f"Missing test {test['name']} for {symbol}")
                tests.append(f"{test['file']}#{test['name']}")
            digest = hashlib.sha256()
            for path, text in sorted(evidence.items()):
                digest.update(path.relative_to(root).as_posix().encode())
                digest.update(b"\0")
                digest.update(text.replace("\r\n", "\n").encode())
                digest.update(b"\0")
            entries[identity] = {
                "status": "mapped",
                "schemaVersion": 1,
                "cppSymbol": symbol,
                "implementationSource": f"{entry['implementationFile']}:{implementation.count(chr(10), 0, definitions[0].start()) + 1}",
                "stateOwner": entry["owner"],
                "ownerSource": f"{entry['ownerFile']}:{owner.count(chr(10), 0, owner.index(entry['ownerAnchor'])) + 1}",
                "writes": entry["writes"],
                "reads": entry.get("reads", []),
                "tests": tests,
                "inputHash": digest.hexdigest(),
                "evidence": "static-binding+reviewed-owner+test-reference",
                "limitations": entry["limitations"],
            }
            if enabled_modules is None or entry["module"] in enabled_modules:
                if not any(contract.module == entry["module"] and contract.cpp_symbol == symbol for contract in contracts):
                    raise ValueError(f"No enabled direct binding for architecture symbol: {symbol}")
    for contract in contracts:
        identity = (contract.module, contract.cpp_symbol)
        if identity in entries:
            # Include the real binding site in the version fingerprint. Aliases
            # retain distinct binding sources but share the implementation.
            mapping = dict(entries[identity])
            binding_path = root / contract.source.rsplit(":", 1)[0]
            binding_text = sources[binding_path].replace("\r\n", "\n")
            mapping["inputHash"] = hashlib.sha256(
                (mapping["inputHash"] + "\0" + contract.source + "\0" + binding_text).encode()
            ).hexdigest()
            contract.architecture = mapping
