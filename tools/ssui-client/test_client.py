import hashlib
import io
import tempfile
import threading
import unittest
from pathlib import Path
from unittest.mock import Mock
from client import GenerationClient, MaterialDocument
from editor import Engine

PNG = b'\x89PNG\r\n\x1a\nexample'
ART = {'kind': 'image', 'mimeType': 'image/png', 'url': '/api/generation/v1/artifacts/one',
       'sha256': hashlib.sha256(PNG).hexdigest()}


class ClientTests(unittest.TestCase):
    def test_mcp_tool_name_does_not_conflict_with_argument_name(self):
        engine = Engine.__new__(Engine)
        engine.rpc = Mock(return_value={'content': [{'type': 'text', 'text': 'ok'}]})
        self.assertEqual(engine.call('eve_host_vm_register', name='VM', source=''), 'ok')
        self.assertEqual(engine.rpc.call_args.args[1]['arguments']['name'], 'VM')

    def test_apply_undo_persistence_and_conflict(self):
        with tempfile.TemporaryDirectory() as root:
            doc = MaterialDocument(root)
            relative = doc.stage(ART, PNG, 0)
            self.assertIsNone(doc.state['baseColor'])
            doc.apply()
            self.assertEqual(MaterialDocument(root).state['baseColor'], relative)
            stale = MaterialDocument(root)
            stale.stage(ART, PNG, 1)
            doc.undo()
            with self.assertRaises(ValueError):
                stale.apply()
            self.assertIsNone(MaterialDocument(root).state['baseColor'])
            self.assertTrue((Path(root)/relative).exists())

    def test_corruption_never_staged(self):
        with tempfile.TemporaryDirectory() as root:
            doc = MaterialDocument(root)
            with self.assertRaises(ValueError):
                doc.stage(ART, PNG+b'bad', 0)
            self.assertIsNone(doc.candidate)
            self.assertEqual(list((Path(root)/'generated').iterdir()), [])

    def test_artifact_origin_and_checksum(self):
        client = GenerationClient('http://localhost:7423')
        with self.assertRaises(ValueError):
            client.download({**ART, 'url': 'http://evil.invalid/file'})
        client.opener = Mock()
        client.opener.open.return_value = io.BytesIO(PNG)
        self.assertEqual(client.download(ART), PNG)
        client.opener.open.return_value = io.BytesIO(PNG+b'bad')
        with self.assertRaises(ValueError):
            client.download(ART)

    def test_cancel_calls_server(self):
        client = GenerationClient('http://localhost:7423')
        client.request = Mock(side_effect=[{'jobId': 'one', 'state': 'running', 'progress': 0}, {}])
        cancel = threading.Event()
        cancel.set()
        with self.assertRaisesRegex(RuntimeError, 'cancelled'):
            client.generate('recipe@1', {}, {}, cancel, lambda *_: None)
        self.assertEqual(client.request.call_args.args, ('jobs/one/cancel', {}))

    def test_timeout_cancels_and_failed_jobs_raise(self):
        client = GenerationClient('http://localhost:7423')
        client.request = Mock(side_effect=[{'jobId':'one', 'state':'running', 'progress':0}, {}])
        with self.assertRaises(TimeoutError):
            client.generate('recipe@1', {}, {}, threading.Event(), lambda *_: None, timeout=0)
        self.assertEqual(client.request.call_args.args, ('jobs/one/cancel', {}))
        client.request = Mock(return_value={'jobId':'two', 'state':'failed', 'progress':0, 'error':'model unavailable'})
        with self.assertRaisesRegex(RuntimeError, 'model unavailable'):
            client.generate('recipe@1', {}, {}, threading.Event(), lambda *_: None)


if __name__ == '__main__':
    unittest.main()
