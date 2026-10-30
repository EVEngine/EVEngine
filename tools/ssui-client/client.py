"""SSUI v1 HTTP client and EVEngine-owned material document. Standard library only."""
import hashlib
import json
import os
import time
import uuid
from pathlib import Path
from urllib.parse import urljoin, urlsplit, quote
from urllib.request import Request, build_opener, HTTPRedirectHandler


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError('Unexpected redirect from generation service')


class GenerationClient:
    def __init__(self, server, timeout=10):
        self.server = server.rstrip('/') + '/'
        if urlsplit(self.server).scheme not in ('http', 'https'):
            raise ValueError('Server must be an HTTP(S) URL')
        self.base = urljoin(self.server, '/api/generation/v1/')
        self.client_id = 'evengine-' + str(uuid.uuid4())
        self.timeout = timeout
        self.opener = build_opener(NoRedirect())

    def request(self, path, body=None):
        request = Request(urljoin(self.base, path),
                          data=None if body is None else json.dumps(body).encode(),
                          headers={'Content-Type': 'application/json'})
        with self.opener.open(request, timeout=self.timeout) as response:
            raw = response.read(1024 * 1024 + 1)
        if len(raw) > 1024 * 1024:
            raise ValueError('Service response too large')
        return json.loads(raw)

    def discover(self):
        caps = self.request('capabilities')
        if caps.get('protocol') != 'ssui-generation' or str(caps.get('version')) != '1':
            raise ValueError('Unsupported generation protocol')
        return self.request('recipes')['items']

    def generate(self, recipe, inputs, context, cancel, progress, timeout=120):
        job = self.request('jobs', {'recipe': recipe, 'inputs': inputs,
            'context': context, 'clientId': self.client_id, 'idempotencyKey': str(uuid.uuid4())})
        path = 'jobs/' + quote(job['jobId'], safe='')
        deadline = time.monotonic() + timeout
        while True:
            progress(job['state'], job['progress'])
            if cancel.is_set():
                if job['state'] not in ('succeeded', 'failed', 'cancelled'):
                    self.request(path + '/cancel', {})
                raise RuntimeError('Generation cancelled')
            if job['state'] == 'succeeded':
                return job
            if job['state'] in ('failed', 'cancelled'):
                raise RuntimeError(job.get('error') or job['state'])
            if time.monotonic() > deadline:
                self.request(path + '/cancel', {})
                raise TimeoutError('Generation timed out: ' + job['jobId'])
            cancel.wait(.2)
            job = self.request(path)

    def download(self, artifact):
        if artifact.get('kind') != 'image' or artifact.get('mimeType') != 'image/png':
            raise ValueError('This material editor accepts PNG images only')
        url = urljoin(self.server, artifact['url'])
        if urlsplit(url)[:2] != urlsplit(self.server)[:2]:
            raise ValueError('Cross-origin artifact URL is not supported')
        with self.opener.open(url, timeout=self.timeout) as response:
            data = response.read(16 * 1024 * 1024 + 1)
        if len(data) > 16 * 1024 * 1024 or not data.startswith(b'\x89PNG\r\n\x1a\n'):
            raise ValueError('Invalid or oversized PNG')
        if hashlib.sha256(data).hexdigest() != artifact['sha256']:
            raise ValueError('Artifact SHA256 mismatch')
        return data


def atomic_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False), encoding='utf-8')
    os.replace(temp, path)


class MaterialDocument:
    """Owns baseColor binding/revision/history; the generation server knows none of these."""
    def __init__(self, project):
        self.root = Path(project).resolve()
        (self.root / 'generated').mkdir(parents=True, exist_ok=True)
        self.path = self.root / 'generated/material.json'
        self.state = json.loads(self.path.read_text()) if self.path.exists() else {
            'revision': 0, 'baseColor': None, 'history': []}
        self.candidate = None

    def check_revision(self, expected):
        disk = json.loads(self.path.read_text()) if self.path.exists() else self.state
        if disk['revision'] != expected or self.state['revision'] != expected:
            raise ValueError('Material changed; reopen the editor before applying')

    def stage(self, artifact, data, revision):
        self.check_revision(revision)
        digest = hashlib.sha256(data).hexdigest()
        if digest != artifact['sha256']:
            raise ValueError('Artifact SHA256 mismatch')
        relative = 'generated/' + digest + '.png'
        target = self.root / relative
        temp = target.with_suffix('.tmp')
        temp.write_bytes(data)
        os.replace(temp, target)
        self.candidate = {'path': relative, 'revision': revision, 'artifact': artifact}
        return relative

    def apply(self):
        if self.candidate is None:
            raise ValueError('Generate a candidate first')
        self.check_revision(self.candidate['revision'])
        updated = {**self.state, 'revision': self.state['revision'] + 1,
            'baseColor': self.candidate['path'],
            'history': self.state['history'] + [self.state['baseColor']]}
        atomic_json(self.path, updated)
        self.state = updated
        self.candidate = None
        return self.state['baseColor']

    def undo(self):
        self.check_revision(self.state['revision'])
        if not self.state['history']:
            raise ValueError('Nothing to undo')
        updated = {**self.state, 'revision': self.state['revision'] + 1,
            'baseColor': self.state['history'][-1], 'history': self.state['history'][:-1]}
        atomic_json(self.path, updated)
        self.state = updated
        self.candidate = None
        return self.state['baseColor']
