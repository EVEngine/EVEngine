#!/usr/bin/env python3
"""Compare representative template objects against a git ref; no engine deps needed.

Usage: python3 scripts/check_template_codegen.py [--baseline HEAD] [--cxx c++]
Sizes include debug information and are compiler/platform specific, not engine totals.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {"common/Module.h": "src/engine/common/Module.h",
           "rx/Rx.h": "src/modules/rx/Rx.h"}
MODULE = '#include "common/Module.h"\n' + '\n'.join(
    f'''struct M{i} : eve::Module {{ std::string getName() const override {{ return "M{i}"; }} }};
M{i}* get{i}() {{ return eve::ModuleManager::getInstance<M{i}>("M{i}"); }}
M{i}* require{i}() {{ return eve::ModuleManager::requireInstance<M{i}>("M{i}"); }}'''
    for i in range(8))
RX = r'''
#include "rx/Rx.h"
#include <cassert>
#include <stdexcept>
using namespace eve::rx;
template class eve::rx::Subject<int>;
template class eve::rx::Subject<double>;
template class eve::rx::Subject<std::string>;
template class eve::rx::Subject<Value>;
struct AddressOverload {
    int value;
    const AddressOverload* operator&() const { return nullptr; }
};
int main() {
    Subject<int> source;
    int sum = 0, errors = 0, completions = 0;
    Observer<int> observer;
    observer.onNext = [&](const int& v) { sum += v; };
    observer.onError = [&](const std::string& e) { assert(e == "error"); ++errors; };
    observer.onCompleted = [&] { ++completions; };
    auto sub = source.subscribe(observer);
    source.onNext(3);
    assert(sum == 3 && source.observerCount() == 1);
    sub.dispose();
    source.onNext(4);
    assert(sum == 3 && !source.hasObservers());
    auto sub2 = source.subscribe(observer);
    source.onError("error");
    source.onNext(5);
    assert(sum == 3 && errors == 1 && !source.hasObservers());
    auto late = source.subscribe(observer);
    assert(completions == 1);

    Subject<int> completed;
    auto c = completed.subscribe(observer);
    completed.onCompleted();
    completed.onNext(6);
    assert(completions == 2 && sum == 3);

    // Callbacks run outside the mutex and may unsubscribe/reenter dispatch.
    Subject<int> reentrant;
    Subscription self;
    int calls = 0;
    self = reentrant.subscribe([&](const int&) {
        ++calls;
        self.dispose();
        reentrant.onNext(2);
    });
    reentrant.onNext(1);
    assert(calls == 1);

    // Callback exceptions must propagate without retaining the mutex.
    Subject<int> throwing;
    auto t = throwing.subscribe([](const int&) { throw std::runtime_error("callback"); });
    bool caught = false;
    try { throwing.onNext(1); } catch (const std::runtime_error&) { caught = true; }
    assert(caught);
    t.dispose();
    assert(!throwing.hasObservers());

    Subject<std::string> strings;
    std::string received;
    auto s = strings.subscribe([&](const std::string& v) { received = v; });
    strings.onNext("payload");
    assert(received == "payload");

    Subject<AddressOverload> addresses;
    int receivedValue = 0;
    auto a = addresses.subscribe([&](const AddressOverload& v) { receivedValue = v.value; });
    addresses.onNext(AddressOverload{42});
    assert(receivedValue == 42);
}
'''


def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)


def text_size(obj):
    """Report executable text separately from DWARF, shader data and metadata."""
    flag = '-m' if sys.platform == 'darwin' else '-A'
    output = subprocess.check_output(['size', flag, str(obj)], text=True)
    if sys.platform == 'darwin':
        sections = re.findall(r'Section (?:\(__TEXT, __text\)|__text):\s*(\d+)', output)
    else:
        sections = re.findall(r'^\.text\S*\s+(\d+)', output, re.M)
    if not sections:
        raise RuntimeError(f'Cannot find executable text in size output: {output}')
    return sum(map(int, sections))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', default='HEAD')
    parser.add_argument('--cxx', default=os.environ.get('CXX', 'c++'))
    parser.add_argument('--baseline-dir', type=Path,
                        help='Header snapshot containing common/Module.h and rx/Rx.h; overrides --baseline')
    parser.add_argument('--probe', choices=['all', 'module', 'rx', 'rx-ops'], default='all')
    args = parser.parse_args()
    run(args.cxx, '--version')
    with tempfile.TemporaryDirectory(prefix='ev-template-codegen-') as tmp:
        tmp = Path(tmp)
        before = tmp / 'before'
        for local, repo in HEADERS.items():
            path = before / local
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((args.baseline_dir / local).read_bytes() if args.baseline_dir else
                             subprocess.check_output(['git', 'show', f'{args.baseline}:{repo}'], cwd=ROOT))
        probes = [('module', MODULE), ('rx', RX),
                  ('rx-ops', (ROOT / 'scripts/tests/template_rx_operators.cpp').read_text())]
        for name, code in probes:
            if args.probe not in ['all', name]:
                continue
            src = tmp / f'{name}.cpp'
            src.write_text(code)
            for opt in ['-O0', '-O2']:
                sizes = []
                texts = []
                for label, includes in [('before', [before]), ('after', [])]:
                    obj = tmp / f'{name}-{label}-{opt}.o'
                    flags = [args.cxx, '-std=c++20', opt, '-g', '-pthread']
                    for path in includes + [ROOT / 'src/engine', ROOT / 'src/modules']:
                        flags.extend(['-I', str(path)])
                    run(*flags, '-c', str(src), '-o', str(obj))
                    sizes.append(obj.stat().st_size)
                    texts.append(text_size(obj))
                    if name.startswith('rx'):
                        exe = obj.with_suffix('.exe')
                        run(args.cxx, '-pthread', str(obj), '-o', str(exe))
                        run(str(exe))
                print(f'{name:6} {opt}: {sizes[0]:,} -> {sizes[1]:,} bytes '
                      f'({(sizes[1] / sizes[0] - 1) * 100:+.1f}%); '
                      f'text {texts[0]:,} -> {texts[1]:,} bytes', flush=True)
    print('Selected probes passed; Rx probes ran both versions at both optimization levels.')


if __name__ == '__main__':
    main()
