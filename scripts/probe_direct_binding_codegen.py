#!/usr/bin/env python3
"""Measure full Rx/Network TU plus direct binding helper against a saved baseline source.

Save the module source before the binding change, then pass --baseline-source PATH.
Use --module network to measure Network.cpp (also requires Poco and zeroerr headers).
Current headers are shared by both sides of the comparison.
Requires initialized Squirrel/simplesquirrel headers, no full engine build.
"""
import utf8_stdio
import argparse
from pathlib import Path
import subprocess
import tempfile
from check_template_codegen import ROOT, text_size


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline-source', required=True, type=Path)
    parser.add_argument('--dependency-checkout', type=Path, default=ROOT)
    parser.add_argument('--cxx', default='c++')
    parser.add_argument("--module", choices=["rx", "network"], default="rx")
    args = parser.parse_args()
    deps = args.dependency_checkout / 'third-party'
    flags = [args.cxx, '-std=c++20', '-D_SQ64', '-g']
    flags += ['-I' + str(p) for p in [ROOT/'src/engine', ROOT/'src/modules',
              deps/'simplesquirrel/include', deps/'squirrel/include']]
    flags += ['-I' + str(args.dependency_checkout/'external/zeroerr/include')]
    if args.module == 'network':
        flags += ['-I' + str(deps/'poco'/part/'include') for part in ['Foundation', 'Net']]
    source = ROOT / ('src/modules/rx/Rx.cpp' if args.module == 'rx' else 'src/modules/network/Network.cpp')
    with tempfile.TemporaryDirectory(prefix='ev-direct-codegen-') as tmp:
        for opt in ['-O0', '-O2']:
            sizes = {}
            for label, src in [('before', args.baseline_source),
                               ('after', source),
                               ('helper', ROOT/'src/engine/common/ScriptBinding.cpp')]:
                obj = Path(tmp) / (label + '.o')
                subprocess.run(flags + [opt, '-c', str(src), '-o', str(obj)], check=True)
                sizes[label] = (obj.stat().st_size, text_size(obj))
            print(opt, sizes)
            print('after total (obj, text):', tuple(
                sizes['after'][i] + sizes['helper'][i] for i in range(2)))


if __name__ == '__main__':
    utf8_stdio.enable_utf8_stdio()
    main()
