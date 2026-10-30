"""Native EVEngine material preview/editor driven by a deterministic Python controller."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

from client import GenerationClient, MaterialDocument


class Engine:
    def __init__(self, executable, project, *, worker=False):
        command = [str(Path(executable).resolve()), 'mcp', '--timeout-ms', '30000', '--root', str(project)]
        if worker:
            command.append('--worker')
        self.process = subprocess.Popen(command,
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1)
        self.responses = queue.Queue()
        self.counter = 0
        threading.Thread(target=self.read, daemon=True).start()
        try:
            self.rpc('initialize', {'protocolVersion': '2024-11-05', 'capabilities': {},
                                  'clientInfo': {'name': 'evengine-generation-client', 'version': '0.1'}})
            self.process.stdin.write(json.dumps({'jsonrpc': '2.0', 'method': 'notifications/initialized'})+'\n')
            self.process.stdin.flush()
        except BaseException:
            self.close()
            raise

    def read(self):
        try:
            for line in self.process.stdout:
                try:
                    self.responses.put(json.loads(line))
                except ValueError:
                    pass
        finally:
            self.responses.put(None)

    def rpc(self, method, params):
        self.counter += 1
        request_id = self.counter
        self.process.stdin.write(json.dumps({'jsonrpc': '2.0', 'id': request_id, 'method': method, 'params': params})+'\n')
        self.process.stdin.flush()
        deadline = time.monotonic() + 30
        while True:
            response = self.responses.get(timeout=max(.01, deadline-time.monotonic()))
            if response is None:
                raise RuntimeError('EVEngine exited')
            if response.get('id') == request_id:
                if 'error' in response:
                    raise RuntimeError(str(response['error']))
                return response['result']
            if time.monotonic() >= deadline:
                raise TimeoutError('EVEngine response timeout')

    def call(self, tool_name, **arguments):
        result = self.rpc('tools/call', {'name': tool_name, 'arguments': arguments})
        text = '\n'.join(c.get('text', '') for c in result.get('content', []))
        if result.get('isError') or text.startswith('error:'):
            raise RuntimeError(text)
        try:
            return json.loads(text)
        except ValueError:
            return text

    def close(self):
        if self.process.poll() is None:
            self.process.stdin.close()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=5)
        return self.process.returncode


class Editor:
    def __init__(self, engine, client, document):
        self.engine, self.client, self.document = engine, client, document
        self.pool = ThreadPoolExecutor(max_workers=1)
        self.cancel = threading.Event()
        self.updates = queue.Queue()
        self.future = None
        self.inputs = {'prompt': 'weathered red brick', 'seed': 0, 'size': 256}
        self.recipe = 'mock.checker@1'
        self.job_revision = 0

    def setup(self):
        self.engine.call('eve_host_window_open', title='EVEngine - SSUI Material Lab', width=1000, height=680)
        self.engine.call('eve_host_vm_register', name='GenerationVM', source='::GenerationVM <- { status = \"Connecting...\" };')
        scale = 0.5 if sys.platform == 'darwin' else 1.0
        self.engine.call('eve_host_script', source='eve.UI().setScale('+str(scale)+');')
        children = [{'type': 'label', 'id': 'intro', 'text': 'SSUI / PNG material'},
            {'type': 'input', 'id': 'prompt', 'label': 'Description', 'value': self.inputs['prompt']},
            {'type': 'slider', 'id': 'seed', 'label': 'Seed', 'value': 0, 'min': 0, 'max': 1000},
            {'type': 'text', 'id': 'status', 'bind:label': 'vm.status'},
            {'type': 'progress', 'id': 'progress', 'value': 0.0}]
        for key, label in [('generate', 'Generate'), ('cancel', 'Cancel generation'), ('preview', 'Preview candidate'),
                           ('restore', 'Exit preview'), ('apply', 'Apply to material'), ('undo', 'Undo application')]:
            children.append({'type': 'button', 'id': key, 'label': label})
        self.engine.call('eve_host_editor_apply', editor={'id': 'generation', 'title': 'Material', 'vm': 'GenerationVM',
            'x': 10, 'y': 10, 'width': 390*scale, 'height': 640*scale,
            'children': children})
        self.engine.call('eve_host_script', source='''
            ::ssuiGfx <- eve.Graphics();
            ::ssuiTexture <- null;
            ::eve_host_render <- function() {
                if (::ssuiTexture != null)
                    ::ssuiGfx.drawTexturedRect(::ssuiTexture, 420.0, 80.0, 512.0, 512.0, 1.0, 1.0, 1.0, 1.0);
                else
                    ::ssuiGfx.drawSolidRect(420.0, 80.0, 512.0, 512.0, 0.3, 0.32, 0.35, 1.0);
            };
        ''')
        self.show(self.document.state['baseColor'])
        self.future = self.pool.submit(self.client.discover)
        self.operation = 'discover'

    def status(self, text):
        print(text, flush=True)
        self.engine.call('eve_host_script', source='::GenerationVM.status = '+json.dumps(text)+';')

    def show(self, relative):
        if relative is None:
            source = '::ssuiTexture = null;'
        else:
            # Only client-created, content-addressed paths are used in script literals.
            path = Path(relative)
            if path.parent.as_posix() != 'generated' or len(path.stem) != 64 or any(c not in '0123456789abcdef' for c in path.stem) or path.suffix != '.png':
                raise ValueError('Invalid material asset path')
            source = 'local next = ::ssuiGfx.newTextureFromFile(' + json.dumps(relative) + '); if (next == null) throw "Texture load failed"; ::ssuiTexture = next;'
        self.engine.call('eve_host_script', source=source)

    def generate(self):
        if self.future:
            self.status('A request is already running')
            return
        self.cancel.clear()
        self.document.candidate = None
        self.job_revision = self.document.state['revision']
        self.show(self.document.state['baseColor'])
        self.operation = 'generate'
        self.future = self.pool.submit(self.worker, dict(self.inputs), self.job_revision)
        self.status('Generating...')

    def worker(self, inputs, revision):
        job = self.client.generate(self.recipe, inputs, {'material': 'baseColor', 'revision': revision},
            self.cancel, lambda state, progress: self.updates.put((state, progress)))
        if not job['artifacts']:
            raise ValueError('Generation produced no artifacts')
        artifact = job['artifacts'][0]
        content = self.client.download(artifact)
        if self.cancel.is_set():
            raise RuntimeError('Generation cancelled')
        return artifact, content

    def action(self, name):
        if name == 'cancel':
            self.cancel.set()
        elif name == 'generate':
            self.generate()
        elif self.future:
            self.status('Wait for generation or cancel it first')
        elif name == 'preview':
            if not self.document.candidate:
                raise ValueError('Generate a candidate first')
            self.document.check_revision(self.document.candidate['revision'])
            self.show(self.document.candidate['path'])
            self.status('Temporary preview (not applied)')
        elif name == 'restore':
            self.show(self.document.state['baseColor'])
            self.status('Showing applied material')
        elif name == 'apply':
            if not self.document.candidate:
                raise ValueError('Generate a candidate first')
            # Validate texture loading before committing the binding.
            self.show(self.document.candidate['path'])
            try:
                self.document.apply()
            except Exception:
                self.show(self.document.state['baseColor'])
                raise
            self.status('Applied; saved generated/material.json')
        elif name == 'undo':
            self.show(self.document.undo())
            self.status('Application undone')

    def tick(self):
        while not self.updates.empty():
            state, progress = self.updates.get_nowait()
            self.engine.call('eve_host_editor_set_value', editor='generation', widget='progress', value=progress/100)
            self.status(state)
        if self.future and self.future.done():
            future, self.future = self.future, None
            try:
                result = future.result()
                if self.operation == 'discover':
                    if not any(item['id'] == self.recipe for item in result):
                        raise ValueError('This demo requires mock.checker@1')
                    self.status('Ready / mock.checker@1')
                else:
                    if self.cancel.is_set():
                        raise RuntimeError('Generation cancelled')
                    self.document.stage(*result, self.job_revision)
                    self.status('Candidate ready; preview or apply')
            except Exception as exc:
                self.status('Error: ' + str(exc))
        for event in self.engine.call('eve_host_events', editor='generation'):
            try:
                if event['type'] == 'change' and event['widget'] in self.inputs:
                    value = event['value']
                    self.inputs[event['widget']] = int(value) if event['widget'] == 'seed' else value
                elif event['type'] == 'click':
                    self.action(event['widget'])
            except Exception as exc:
                self.status('Error: ' + str(exc))

    def close(self):
        self.cancel.set()
        self.pool.shutdown(wait=True, cancel_futures=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--eve', required=True, help='EVEngine executable with editor-host support')
    parser.add_argument('--project', required=True, type=Path, help='Project directory for generated assets/material')
    parser.add_argument('--server', default='http://127.0.0.1:7423')
    parser.add_argument('--smoke', action='store_true', help='Generate, preview, apply, undo and save an engine screenshot')
    args = parser.parse_args()
    document = MaterialDocument(args.project)
    engine = Engine(args.eve, document.root)
    editor = Editor(engine, GenerationClient(args.server), document)
    try:
        initial_revision = document.state['revision']
        editor.setup()
        if args.smoke:
            deadline = time.monotonic() + 150
            while editor.future:
                editor.tick()
                if time.monotonic() > deadline:
                    raise TimeoutError('Discovery timeout')
                time.sleep(.05)
            editor.generate()
            while editor.future:
                editor.tick()
                if time.monotonic() > deadline:
                    raise TimeoutError('Smoke timeout')
                time.sleep(.05)
            editor.action('preview')
            editor.action('apply')
            engine.call('eve_host_capture', path=str(document.root / 'applied.png'))
            editor.action('undo')
            assert document.state['revision'] == initial_revision + 2
            print('SMOKE PASS: real engine texture preview, apply and undo', flush=True)
        else:
            while engine.process.poll() is None:
                editor.tick()
                time.sleep(.1)
    except KeyboardInterrupt:
        pass
    finally:
        editor.close()
        engine.close()


if __name__ == '__main__':
    main()
