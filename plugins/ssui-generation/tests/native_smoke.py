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
    parser.add_argument('--project', required=True, type=Path, help='Disposable installed plugin project')
    args = parser.parse_args()
    root = args.project.resolve()
    document = root / 'generated/material.json'
    before = json.loads(document.read_text()) if document.exists() else {'revision': 0, 'baseColor': ''}
    engine = Engine(args.eve, root)
    try:
        state = engine.call('eve_host_editor_state', id='ssui')
        assert state['editors'][0]['vm'] == 'SSUIMaterialVM'
        engine.call('eve_host_script', source='SSUIMaterialVM.generate("ssui","generate");')
        deadline = time.monotonic()+130
        while time.monotonic()<deadline:
            status = engine.call('eve_host_editor_state', id='ssui')['editors'][0]['values'].get('status','')
            if status == 'Candidate ready':
                break
            if status.startswith('Error:'):
                raise RuntimeError(status)
            time.sleep(.2)
        else:
            raise TimeoutError('Generation failed to produce a candidate')
        engine.call('eve_host_script', source='SSUIMaterialVM.preview("ssui","preview"); SSUIMaterialVM.apply("ssui","apply");')
        applied = json.loads(document.read_text())
        assert applied['revision'] == before['revision']+1
        assert (root/applied['baseColor']).read_bytes().startswith(b'\x89PNG\r\n\x1a\n')
        engine.call('eve_host_capture', path=str(root/'native-applied.png'))
        engine.call('eve_host_script', source='SSUIMaterialVM.undo("ssui","undo");')
        undone = json.loads(document.read_text())
        assert undone['revision'] == before['revision']+2 and undone['baseColor'] == before['baseColor']
        engine.call('eve_host_script', source='ssui.start("mock.checker@1","{}","{}"); ssui.cancel(); ssui.shutdown(); if(ssui.candidate()!="") throw "Cancelled result leaked";')
        print('PASS: plugin load, native generation/preview, persistence, undo and cancellation')
    finally:
        engine.close()


if __name__=='__main__':
    main()
