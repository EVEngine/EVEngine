#!/usr/bin/env python3
"""Measure a proposal only: erase captureless binding lambdas to function pointers.

Extracts 8 integer writer and 8 integer reader bindings from Network.cpp and
34 LayoutConfig bindings from Card.cpp, using real engine and library declarations. Compiles objects; no VM runtime
or whole-engine behavior/performance claim. Does not modify production sources.
"""
import utf8_stdio
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from check_template_codegen import ROOT, text_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dependency-checkout', type=Path, default=ROOT)
    parser.add_argument('--group', choices=['all', 'writer', 'reader', 'cfgCls'], default='all')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    dep = args.dependency_checkout.resolve() / 'third-party'
    source = (ROOT / 'src/modules/network/Network.cpp').read_text(encoding='utf-8')
    groups = {
        'writer': source[source.index('    writer.addFunc("writeU8"'):source.index('    writer.addFunc("writeF32"')],
        'reader': source[source.index('    reader.addFunc("u8"'):source.index('    reader.addFunc("f32"')],
    }
    card_source = (ROOT / 'src/modules/card/Card.cpp').read_text(encoding='utf-8')
    groups['cfgCls'] = card_source[card_source.index('    cfgCls.addFunc("getCardW"'):card_source.index('    // CardData', card_source.index('    cfgCls.addFunc("getCardW"'))]
    flags = [args.cxx, '-std=c++20', '-g', '-Isrc/modules']
    for inc in [dep / 'simplesquirrel/include', dep / 'squirrel/include', dep / 'glm']:
        flags += ['-isystem', str(inc)]
    print(subprocess.check_output([args.cxx, '--version'], text=True).splitlines()[0])
    with tempfile.TemporaryDirectory(prefix='ev-binding-codegen-') as tmp:
        tmp = Path(tmp)
        for name, block in groups.items():
            if args.group not in ['all', name]:
                continue
            expected = 34 if name == 'cfgCls' else 8
            if block.count('.addFunc(') != expected or block.count(', [](') != expected:
                raise RuntimeError('Bindings changed; review extraction before measuring')
            for opt in ['-O0', '-O2']:
                rows = []
                for label, body in [('lambda', block), ('function-pointer', block.replace(', [](', ', +[]('))]:
                    path = tmp / f'{name}-{label}.cpp'
                    obj = path.with_suffix('.o')
                    header = 'card/CardTypes.h' if name == 'cfgCls' else 'network/NetStream.h'
                    namespace = 'eve::card' if name == 'cfgCls' else 'eve::network'
                    path.write_text(f'#include "{header}"\n#include <simplesquirrel/simplesquirrel.hpp>\n'
                                    f'using namespace {namespace};\n'
                                    f'void bind_{name}(ssq::Class& {name}) {{\n' + body + '\n}\n', encoding='utf-8')
                    result = subprocess.run(flags + [opt, '-c', str(path), '-o', str(obj)],
                                            cwd=ROOT, capture_output=True, text=True)
                    if result.returncode:
                        raise RuntimeError(result.stdout + result.stderr)
                    rows.append((obj.stat().st_size, text_size(obj)))
                print(f'{name} {opt}: obj {rows[0][0]:,} -> {rows[1][0]:,}; '
                      f'text {rows[0][1]:,} -> {rows[1][1]:,}', flush=True)


if __name__ == '__main__':
    utf8_stdio.enable_utf8_stdio()
    main()
