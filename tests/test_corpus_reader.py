#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Synthetic correlation regressions; never counted as real corpus streams."""
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

READER = pathlib.Path(sys.argv[sys.argv.index('--reader') + 1]).resolve()
sys.argv = [sys.argv[0]]


class ReplayTests(unittest.TestCase):
    def replay(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / 'synthetic.jsonl'
            path.write_bytes(data)
            run = subprocess.run([str(READER), str(path), 'codex-app-server'],
                                 capture_output=True, check=True)
        return json.loads(run.stdout)

    def test_sequence_duplicates_zero_and_cancel(self):
        result = self.replay(
            b'{"id":3,"method":"synthetic"}\n'
            b'{"id":1,"result":null}\n'
            b'{"id":1,"result":null}\n'
            b'{"id":0,"result":null}\n'
            b'{"id":3,"error":{"code":-1,"message":"synthetic"}}\n')
        self.assertEqual(result['correlation'], {
            'opened': 3, 'settled': 2, 'duplicate_responses': 1,
            'unknown_zero': 1, 'invalid_argument': 4, 'cancelled': 1,
            'release_lost': 0})
        self.assertEqual(result['missing_version'], 5)

    def test_streams_use_independent_tables(self):
        data = b'{"id":1,"result":null}\n'
        for _ in range(2):
            result = self.replay(data)['correlation']
            self.assertEqual(result['opened'], 1)
            self.assertEqual(result['settled'], 1)
            self.assertEqual(result['duplicate_responses'], 0)
            self.assertEqual(result['cancelled'], 0)
            self.assertEqual(result['release_lost'], 0)

    def test_zero_is_not_emitted_in_a_zero_only_capture(self):
        result = self.replay(b'{"id":0,"result":null}\n')['correlation']
        self.assertEqual(result['opened'], 0)
        self.assertEqual(result['unknown_zero'], 1)
        self.assertEqual(result['settled'], 0)
        self.assertEqual(result['release_lost'], 0)


if __name__ == '__main__':
    unittest.main()
