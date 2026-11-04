#!/usr/bin/env python3
"""Compose typed MCP providers from the CMake target's compiled C++ sources."""

import argparse
from pathlib import Path
import re

from cpp_source import mask_cpp
import utf8_stdio

DECLARATION = re.compile(r'\bEVE_MCP_TOOL_FAMILY\s*\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)')


def discover(paths: list[Path]) -> list[str]:
    """Find real compiled declarations; strings/comments cannot register families."""
    owners = {}
    for path in paths:
        text = mask_cpp(path.read_text(encoding="utf-8"))
        matches = list(DECLARATION.finditer(text))
        if len(re.findall(r'\bEVE_MCP_TOOL_FAMILY\s*\(', text)) != len(matches):
            raise ValueError(f"{path}: malformed MCP family declaration")
        for match in matches:
            name, _, _, payload = match.groups()
            if payload not in {"TextValue", "ContentEnvelope"}:
                raise ValueError(f"{path}: unknown MCP payload encoding {payload}")
            if name in owners:
                raise ValueError(f"duplicate MCP family {name}: {owners[name]}, {path}")
            owners[name] = path
    return sorted(owners)


def render(names: list[str]) -> str:
    """Emit typed function references so OBJECT/SHARED linkers retain every provider."""
    lines = ['// Generated from compiled MCP provider declarations; do not edit.',
             '#include "devtools/McpToolFamilies.hpp"', '', 'namespace eve::dev {']
    lines.extend(f'McpToolFamily eveMcpToolFamily_{name}();' for name in names)
    lines += ['std::vector<McpToolFamily> compiledMcpToolFamilies() {',
              '    return {' + ', '.join(f'eveMcpToolFamily_{name}()' for name in names) + '};',
              '}', '}  // namespace eve::dev', '']
    return '\n'.join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    paths = [Path(line) for line in args.sources.read_text(encoding="utf-8").splitlines() if line]
    result = render(discover(paths))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    if not args.output.exists() or args.output.read_text(encoding="utf-8") != result:
        args.output.write_text(result, encoding="utf-8")


if __name__ == '__main__':
    utf8_stdio.enable_utf8_stdio()
    main()
