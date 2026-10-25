#!/usr/bin/env python3
"""Measure (without applying) replacing embeddedSpirv<T,N> with dynamic span.

Uses all shader arrays currently passed to the helper, with identical externally
visible wrappers in both versions. Reports obj bytes and executable text bytes.
Requires a C++20 compiler and size; no Vulkan libraries needed.
"""
import utf8_stdio
import os
from pathlib import Path
import re
import subprocess
import tempfile

from check_template_codegen import ROOT, text_size


def main():
    cxx = os.environ.get('CXX', 'c++')
    source = (ROOT / 'src/modules/graphics/vulkan/Graphics.cpp').read_text(encoding='utf-8')
    arrays = sorted(set(re.findall(r'embeddedSpirv\((\w+_spv)\)', source)))
    original = re.search(r'template <typename T, size_t N>\nstd::vector<T> embeddedSpirv.*?\n\}',
                         source, re.S)
    if not original or not arrays:
        raise RuntimeError('embeddedSpirv source changed; update the experiment first')
    proposal = ('std::vector<uint32_t> embeddedSpirv(std::span<const uint32_t> words) '
                '{ return {words.begin(), words.end()}; }')
    includes = '#include <cstdint>\n#include <cstddef>\n#include <vector>\n#include <span>\n'
    for name in arrays:
        includes += f'#include "{ROOT / "src/modules/graphics/shaders" / (name + ".inc")}"\n'
    wrappers = ''.join(f'auto get_{name}() {{ return embeddedSpirv({name}); }}\n' for name in arrays)
    print(f'{len(arrays)} shader arrays; identical input data and wrappers', flush=True)
    with tempfile.TemporaryDirectory(prefix='ev-spirv-codegen-') as tmp:
        for opt in ['-O0', '-O2']:
            for label, body in [('original', original[0]), ('span-proposal', proposal)]:
                path = Path(tmp) / f'{label}.cpp'
                obj = path.with_suffix('.o')
                path.write_text(includes + 'namespace {\n' + body + '\n}\n' + wrappers, encoding='utf-8')
                subprocess.run([cxx, '-std=c++20', opt, '-g', '-c', str(path), '-o', str(obj)], check=True)
                print(f'{opt} {label}: obj={obj.stat().st_size:,}; text={text_size(obj):,}', flush=True)


if __name__ == '__main__':
    utf8_stdio.enable_utf8_stdio()
    main()
