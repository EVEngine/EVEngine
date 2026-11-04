#!/usr/bin/env python3
"""Compare actual recipe-list/decoder template call sites against a git ref.

--dependency-checkout points to an EVEngine checkout with populated dependencies;
all its files are read-only inputs. --poco-build enables recipe behavior tests
against real Poco static libraries (Foundation and JSON). Decoder behavior tests
always use the real medialoader Decoder/Exception implementations. Codegen uses
four real decoder classes; Mpg123 needs a generated config header and is excluded.
"""
import utf8_stdio
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

from check_template_codegen import ROOT, text_size


def run(args):
    result = subprocess.run([str(x) for x in args], cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)


def recipe_body(source):
    start = source.index('        Poco::JSON::Object::Ptr o', source.index('if (name == "eve_procgen_recipes")'))
    end = source.index('        return mcpStringify', start)
    return 'Poco::JSON::Object::Ptr recipes(eve::procgen::Procgen* pg) {\n' + source[start:end] + 'return o;\n}\n'


def decoder_body(source):
    # This helper contains no brace at column zero until its closing brace.
    match = re.search(r'template <typename T>\nstd::unique_ptr<medialoader::Decoder> tryMake.*?\n\}', source, re.S)
    if not match:
        raise RuntimeError('tryMake changed; update extraction before measuring')
    return match[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dependency-checkout', type=Path, default=ROOT)
    parser.add_argument('--poco-build', type=Path)
    parser.add_argument('--baseline', default='HEAD')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    args = parser.parse_args()
    deps = args.dependency_checkout.resolve()
    media = deps / 'third-party/medialoader'
    includes = [deps / 'external/ECS.hpp/src', deps / 'external/zeroerr/include']
    includes += [deps / 'third-party' / p for p in [
        'poco/Foundation/include', 'poco/JSON/include', 'glm', 'simplesquirrel/include', 'squirrel/include']]
    includes += [media / p for p in [
        'include', 'sound-libs', 'sound-libs/libvorbis/include', 'sound-libs/libogg/include',
        'sound-libs/libogg/android', 'sound-libs/libmodplug/src']]
    flags = [args.cxx, '-std=c++20', '-g', '-Isrc/engine', '-Isrc/modules']
    for inc in includes:
        flags += ['-isystem', str(inc)]
    decoder_classes = ['WaveDecoder', 'VorbisDecoder', 'FLACDecoder', 'ModPlugDecoder']
    recipe_headers = '#include "procgen/Procgen.h"\n#include <Poco/JSON/Array.h>\n#include <Poco/JSON/Object.h>\n'
    decoder_headers = '#include <memory>\n#include <medialoader/Exception.h>\n'
    decoder_headers += ''.join(f'#include <medialoader/sound/{c}.h>\n' for c in decoder_classes)
    decoder_wrappers = ''.join(f'auto make{c}(const char*d,size_t n,int b) {{ return tryMake<medialoader::{c}>(d,n,b); }}\n'
                               for c in decoder_classes)
    tasks = [
        ('recipes', 'src/engine/devtools/McpServer.cpp', recipe_body, recipe_headers, '',
         'recipe_lists.inc', 'template_recipe_lists.cpp'),
        ('decoders', 'src/modules/sound/Sound.cpp', decoder_body, decoder_headers, decoder_wrappers,
         'decoder_fallback.inc', 'template_decoder_fallback.cpp')]
    with tempfile.TemporaryDirectory(prefix='ev-cold-template-') as tmp:
        tmp = Path(tmp)
        for name, repo, extract, headers, wrappers, incname, testname in tasks:
            before = subprocess.check_output(['git', 'show', f'{args.baseline}:{repo}'], cwd=ROOT, text=True)
            after = (ROOT / repo).read_text(encoding='utf-8')
            for opt in ['-O0', '-O2']:
                measurements = []
                for label, source in [('before', before), ('after', after)]:
                    body = extract(source)
                    src = tmp / f'{name}-{label}.cpp'
                    obj = src.with_suffix('.o')
                    src.write_text(headers + body + '\n' + wrappers, encoding='utf-8')
                    run(flags + [opt, '-c', src, '-o', obj])
                    measurements.append((obj.stat().st_size, text_size(obj)))
                    (tmp / incname).write_text(body, encoding='utf-8')
                    exe = tmp / f'{name}-{label}-test'
                    test = ROOT / 'scripts/tests' / testname
                    link = flags + [opt, '-I', tmp, test]
                    if name == 'recipes':
                        if not args.poco_build:
                            continue
                        libdir = args.poco_build.resolve() / 'lib'
                        link += [libdir / 'libPocoJSON.a', libdir / 'libPocoFoundation.a', '-pthread']
                    else:
                        link += [media / 'sound/Decoder.cpp', media / 'image/Exception.cpp']
                    run(link + ['-o', exe])
                    run([exe])
                print(f'{name} {opt}: obj {measurements[0][0]:,} -> {measurements[1][0]:,}; '
                      f'text {measurements[0][1]:,} -> {measurements[1][1]:,}', flush=True)
    print('Decoder behavior passed for both versions at O0/O2.')
    print('Recipe behavior passed for both versions at O0/O2.' if args.poco_build else
          'Recipe behavior not run: pass --poco-build; object compilation only.')


if __name__ == '__main__':
    utf8_stdio.enable_utf8_stdio()
    main()
