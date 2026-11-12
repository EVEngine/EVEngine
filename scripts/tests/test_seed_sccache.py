"""Exercise pinned tool downloads without making service requests or sleeping."""

import hashlib
import importlib.util
import io
from email.message import Message
from pathlib import Path
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

PATH = Path(__file__).resolve().parents[2] / ".github/scripts/seed-sccache.py"
SPEC = importlib.util.spec_from_file_location("seed_sccache", PATH)
SEED = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SEED)
PAYLOAD = b"verified release payload"
HASH = hashlib.sha256(PAYLOAD).hexdigest()
ROUTES = ["https://github.com/official/asset", "https://api.github.com/official/asset"]


def failure(code, reason, **headers):
    message = Message()
    for key, value in headers.items():
        message[key.replace("_", "-")] = value
    return HTTPError(ROUTES[0], code, reason, message, io.BytesIO())


class SeedSccacheTests(unittest.TestCase):
    def run_download(self, replies):
        opener = patch.object(SEED.urllib.request, "urlopen", side_effect=replies)
        return opener

    def test_transient_failure_uses_other_official_route_and_checks_hash(self):
        with self.run_download([failure(504, "timeout"), io.BytesIO(PAYLOAD)]) as open_url:
            with patch.object(SEED.time, "sleep") as sleep:
                self.assertEqual(SEED.fetch_verified(ROUTES, HASH), PAYLOAD)
        self.assertEqual([c.args[0].full_url for c in open_url.call_args_list], ROUTES)
        sleep.assert_called_once_with(2)
        with self.run_download([io.BytesIO(b"wrong content")]) as open_url:
            with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
                SEED.fetch_verified(ROUTES, HASH)
        self.assertEqual(open_url.call_count, 1)

    def test_rate_limit_waits_for_both_headers_without_changing_route(self):
        limited = failure(403, "rate limit exceeded", Retry_After="3", X_RateLimit_Reset="1010")
        with self.run_download([limited, io.BytesIO(PAYLOAD)]) as open_url:
            with patch.object(SEED.time, "time", return_value=1000), patch.object(SEED.time, "sleep") as sleep:
                self.assertEqual(SEED.fetch_verified(ROUTES, HASH), PAYLOAD)
        self.assertEqual([c.args[0].full_url for c in open_url.call_args_list], [ROUTES[0], ROUTES[0]])
        sleep.assert_called_once_with(10)

    def test_excessive_or_unknown_limit_and_access_denial_stop_requests(self):
        for error in [
            failure(403, "rate limit exceeded", X_RateLimit_Reset="5000"),
            failure(429, "too many requests"),
            failure(403, "forbidden"),
        ]:
            with self.subTest(code=error.code, reason=error.reason):
                with self.run_download([error]) as open_url:
                    with patch.object(SEED.time, "time", return_value=1000), patch.object(SEED.time, "sleep") as sleep:
                        with self.assertRaises((RuntimeError, HTTPError)):
                            SEED.fetch_verified(ROUTES, HASH)
                self.assertEqual(open_url.call_count, 1)
                sleep.assert_not_called()

    def test_attempt_budget_and_retry_after_on_service_unavailable(self):
        with self.run_download([failure(503, "unavailable", Retry_After="9"), io.BytesIO(PAYLOAD)]):
            with patch.object(SEED.time, "sleep") as sleep:
                self.assertEqual(SEED.fetch_verified(ROUTES, HASH), PAYLOAD)
        sleep.assert_called_once_with(9)
        with self.run_download([failure(504, "timeout") for _ in range(5)]) as open_url:
            with patch.object(SEED.time, "sleep") as sleep:
                with self.assertRaises(HTTPError):
                    SEED.fetch_verified(ROUTES, HASH)
        self.assertEqual(open_url.call_count, 5)
        self.assertEqual(sleep.call_count, 4)


if __name__ == "__main__":
    unittest.main()
