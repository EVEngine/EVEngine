"""Development-only native plugin integration test; runtime has no Python dependency."""
import argparse
import json
from pathlib import Path
import sys
import time

# Reuse the repository's existing stdio MCP test driver, not its HTTP client.
sys.path.insert(0, str(Path(__file__).resolve().parents[3] / 'tools/ssui-client'))
from editor import Engine


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--eve', required=True)
    parser.add_argument('--server', default='http://127.0.0.1:7423')
    parser.add_argument('--project', required=True, type=Path, help='Disposable installed plugin project')
    args = parser.parse_args()
    root = args.project.resolve()
    document = root / 'generated/material.json'
    before = json.loads(document.read_text(encoding='utf-8')) if document.exists() else {'revision': 0, 'baseColor': ''}
    # Test the real worker exit code; the supervisor intentionally kills workers on EOF.
    engine = Engine(args.eve, root, worker=True)
    try:
        engine.call('eve_host_script', source=f'ssuiValue(ssui.configure({json.dumps(args.server)},"."));')
        engine.call('eve_host_script', source='try { eve_host_update(0.016); } catch(e) { throw "SSUI update: " + e.tostring(); }')
        state = engine.call('eve_host_editor_state', id='ssui')
        assert state['editors'][0]['vm'] == 'SSUIMaterialVM'
        engine.call('eve_host_script', source='SSUIMaterialVM.generate("ssui","generate");')
        engine.call('eve_host_script', source='if (!ssui.busy() && ssui.candidate() == "") throw "SSUI generate: " + ssui.status();')
        deadline = time.monotonic() + 130
        while time.monotonic() < deadline:
            status = engine.call('eve_host_editor_state', id='ssui')['editors'][0]['values'].get('status', '')
            if status == 'Candidate ready':
                break
            if status.startswith('Error:'):
                raise RuntimeError(status)
            time.sleep(.2)
        else:
            raise TimeoutError('Generation failed to produce a candidate')
        engine.call('eve_host_script', source='SSUIMaterialVM.preview("ssui","preview"); SSUIMaterialVM.apply("ssui","apply");')
        applied = json.loads(document.read_text(encoding='utf-8'))
        assert applied['schema'] == 'eve.ssui-material' and applied['version'] == 1
        assert applied['revision'] == before['revision'] + 1
        assert (root / applied['baseColor']).read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        engine.call('eve_host_capture', path=str(root / 'native-applied.png'))

        suffix = '.dll' if sys.platform == 'win32' else '.dylib' if sys.platform == 'darwin' else '.so'
        engine.call('eve_host_script', source=f'''
            local missing = ssuiPlugins.loadPlugin("./missing-ssui-library");
            if (missing.ok) throw "Missing plugin accepted";
            local bad = ssui.start("mock.checker@1", "[]", "{{}}");
            if (bad.ok || !bad.checked) throw "Result projection failed";
            ::oldSsui <- ssui;
            oldSsui.shutdown();
            ssuiValue(ssuiPlugins.reload("./ssui_generation{suffix}"));
            ::ssui = eve.SSUIGeneration();
            if (ssui == oldSsui) throw "Reload reused instance";
            ssuiValue(ssui.configure({json.dumps(args.server)}, "."));
            if (ssui.applied() != oldSsui.applied()) throw "Reload lost material";
        ''')
        engine.call('eve_host_script', source='SSUIMaterialVM.undo("ssui","undo");')
        undone = json.loads(document.read_text(encoding='utf-8'))
        assert undone['revision'] == before['revision'] + 2 and undone['baseColor'] == before['baseColor']
        engine.call('eve_host_script', source='''
            ssuiValue(ssui.start("mock.checker@1", "{}", "{}"));
            ssui.requestCancel();
            ssui.shutdown();
            if (ssui.candidate() != "") throw "Cancelled result leaked";
            local completion = ssui.completion();
            if (completion.ok) throw "Cancelled completion succeeded";
        ''')
    finally:
        if engine.process.poll() is None:
            engine.call('eve_host_shutdown')
        code = engine.close()
        if code != 0:
            raise RuntimeError(f'Native host did not shut down cleanly: {code}')
    print('PASS: native load, Result projection, generation/preview, apply/undo, reload, cancellation and shutdown')


if __name__ == '__main__':
    main()
