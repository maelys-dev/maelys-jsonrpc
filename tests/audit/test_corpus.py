#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Synthetic tests for the audit tool; never counted as real captures."""
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("audit_corpus", ROOT / "tools/audit-corpus.py")
corpus = importlib.util.module_from_spec(spec)
spec.loader.exec_module(corpus)
PROBE = pathlib.Path(sys.argv[sys.argv.index("--probe") + 1]).resolve()
sys.argv = [sys.argv[0]]


class AuditTests(unittest.TestCase):
    def probe(self, data):
        result = subprocess.run([str(PROBE)], input=data, capture_output=True, check=True)
        return json.loads(result.stdout)

    def framed(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "synthetic"
            path.write_bytes(data)
            return list(corpus.lines(path))

    def test_duplicate_policy_difference(self):
        result = self.probe(b'{"id":1,"id":2}')
        self.assertTrue(result["codex_accepted"])
        self.assertFalse(result["mcp_accepted"])
        self.assertEqual(result["parse"], "DUPLICATE_KEY")

    def test_classification_profiles(self):
        for data, expected in ((b'{"id":1,"result":{}}', "RESPONSE"),
                               (b'{"method":"echo"}', "NOTIFICATION")):
            result = self.probe(data)
            self.assertEqual(result["classified_strict"], "INVALID")
            self.assertEqual(result["classified_codex"], expected)
            self.assertTrue(result["missing_version"])
            self.assertTrue(result["classification_equal_codex"])
        for version in (b'"1.0"', b'null', b'2'):
            result = self.probe(b'{"jsonrpc":' + version + b',"id":1,"result":{}}')
            self.assertEqual(result["classified_strict"], "INVALID")
            self.assertEqual(result["classified_codex"], "INVALID")
            self.assertFalse(result["missing_version"])

    def test_float_relay_preserves_content(self):
        result = self.probe(b'{"jsonrpc":"2.0","id":1,"result":{"z":1.5e3}}')
        self.assertEqual(result["parse"], "OK")
        self.assertEqual(result["writer"], "OK")
        self.assertTrue(result["equal_codex"])
        self.assertTrue(result["equal_mcp"])
        self.assertEqual(result["non_integer_numbers"], 1)

    def test_integer_canonical_content_equal(self):
        result = self.probe(b'{"z":2,"a":[1,null,"text"]}')
        self.assertEqual(result["writer"], "OK")
        self.assertTrue(result["equal_codex"])
        self.assertTrue(result["equal_mcp"])

    def test_crlf_at_chunk_boundary_and_orphan(self):
        rows = self.framed(b'x' * corpus.MAX_LINE + b'\r\n{}\nlast\r')
        self.assertEqual(rows[0], (corpus.MAX_LINE, b'x' * corpus.MAX_LINE, True))
        self.assertEqual(rows[1], (2, b'{}', True))
        self.assertEqual(rows[2], (5, b'last\r', False))
        split_crlf = self.framed(b'x' * (corpus.MAX_LINE + 1) + b'\r\n')
        self.assertEqual(split_crlf[0][0], corpus.MAX_LINE + 1)
        self.assertEqual(split_crlf[0][1], b'x' * (corpus.MAX_LINE + 1))

    def test_oversize_is_drained_with_bounded_prefix(self):
        rows = self.framed(b'x' * (3 * corpus.MAX_LINE) + b'\n{}\n')
        self.assertEqual(rows[0][0], 3 * corpus.MAX_LINE)
        self.assertLessEqual(len(rows[0][1]), corpus.MAX_LINE + 1)
        self.assertEqual(rows[1], (2, b'{}', True))

    def test_empty_manifest_does_not_prove_real_corpus(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "manifest.json"
            path.write_text(json.dumps({"schema_version": 2}))
            report = corpus.audit(path, PROBE)
        self.assertEqual(report["corpus_prerequisite"], "incomplete")
        self.assertEqual(report["declared_streams"], 0)
        self.assertEqual(len(report["problems"]), 7)

    def test_manifest_path_cannot_escape(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "manifest.json"
            path.write_text(json.dumps({"schema_version": 2}))
            child = pathlib.Path(directory) / "mcp-stdio"
            child.mkdir()
            (child / "MANIFEST.json").write_text(json.dumps({"streams": [
                {"path": "../../escape", "source": "mcp-stdio"}]}))
            with self.assertRaisesRegex(ValueError, "escapes"):
                corpus.audit(path, PROBE)


if __name__ == "__main__":
    unittest.main()
