#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Admission must reject sensitive byte streams before corpus persistence."""
import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("capture", pathlib.Path(__file__).resolve().parents[1] / "tools/capture-corpus.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

class AdmissionTests(unittest.TestCase):
    def test_synthetic_protocol_is_admitted(self):
        capture.admit(b'{"id":1,"params":{"cwd":"/private/tmp/synthetic","text":"synthetic"}}\n')

    def test_personal_path_in_raw_or_escaped_string(self):
        for blob in (b'/Users/synthetic/project\n', b'{"x":"\\/Users\\/synthetic"}',
                     b'not-json "\\u002fUsers\\u002fsynthetic"',
                     b'{"x":"C:\\\\Users\\\\synthetic"}'):
            with self.subTest(blob=blob), self.assertRaisesRegex(ValueError, "personal_path"):
                capture.admit(blob)

    def test_encoded_credential_key_and_nested_bearer(self):
        for blob in (b'{"api\\u005fkey":"synthetic-value"}',
                     b'{"nested":["Bearer syntheticcredential"]}',
                     b'{"id_token":"synthetic-value"}'):
            with self.subTest(blob=blob), self.assertRaisesRegex(ValueError, "credential_pattern"):
                capture.admit(blob)

    def test_no_in_place_anonymization(self):
        blob = bytearray(b'/home/synthetic/project')
        original = bytes(blob)
        with self.assertRaises(ValueError): capture.admit(blob)
        self.assertEqual(bytes(blob), original)

if __name__ == "__main__":
    unittest.main()
