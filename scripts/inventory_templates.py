#!/usr/bin/env python3
"""Lexical inventory of non-ignored C++ template sites, not an instantiation profiler.

Comments and string literals are masked while preserving line numbers. Includes
explicit template introducers, generic lambdas and auto-parameter candidates.
Does not expand macros or load uninitialized submodules / downloaded dependencies.
Run from any directory; output is JSON on stdout.
"""
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
EXTENSIONS = {'.h', '.hpp', '.hh', '.hxx', '.cpp', '.cc', '.cxx', '.inl',
              '.ipp', '.tpp', '.mm', '.ixx', '.cppm', '.cuh', '.inc'}
LITERALS = re.compile(
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=delimiter)"'
    r'|//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)


def mask(source):
    return LITERALS.sub(lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), source)


def inventory():
    paths = set(subprocess.check_output(
        ['git', 'ls-files', '--cached', '--others', '--exclude-standard', '-z'],
        cwd=ROOT).decode().split('\0'))
    records = []
    scanned = 0
    for name in sorted(paths):
        path = ROOT / name
        if path.suffix not in EXTENSIONS or not path.is_file():
            continue
        scanned += 1
        source = path.read_text(encoding='utf-8-sig')
        clean = mask(source)
        for hit in re.finditer(r'\btemplate\s*<', clean):
            start = hit.start()
            cursor = hit.end()
            depth = 1
            while cursor < len(clean) and depth:
                depth += (clean[cursor] == '<') - (clean[cursor] == '>')
                cursor += 1
            end = cursor
            while end < len(clean) and clean[end] not in ';{':
                end += 1
            records.append({'file': name, 'line': clean.count('\n', 0, start) + 1,
                            'kind': 'explicit-template',
                            'declaration': ' '.join(source[start:end].split())})
        for hit in re.finditer(r'\]\s*(?:<[^;{}]*>)?\s*\([^;{}]*?\bauto\b[^;{}]*?\)', clean):
            records.append({'file': name, 'line': clean.count('\n', 0, hit.start()) + 1,
                            'kind': 'generic-lambda',
                            'declaration': ' '.join(source[hit.start():hit.end()].split())})
        # A separate candidate list catches abbreviated function templates;
        # generic lambda candidates are removed by the closing bracket check.
        for hit in re.finditer(r'\b(\w+)\s*\([^;{}()]*\bauto\b[^;{}()]*\)', clean):
            if hit[1] in {'if', 'for', 'while', 'switch', 'sizeof', 'decltype'}:
                continue
            records.append({'file': name, 'line': clean.count('\n', 0, hit.start()) + 1,
                            'kind': 'auto-parameter-candidate',
                            'declaration': ' '.join(source[hit.start():hit.end()].split())})
    return {'scope': 'tracked and non-ignored new C++ source; lexical sites, not emitted specializations',
            'files_scanned': scanned, 'sites': records}


if __name__ == '__main__':
    print(json.dumps(inventory(), ensure_ascii=False, indent=2))
