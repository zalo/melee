#!/usr/bin/env python3
"""The soak report receiver stores valid reports, answers with an id and gives nothing back."""
import gzip
import importlib.util
from http.server import ThreadingHTTPServer
from pathlib import Path
import socket
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

NATIVE = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('soak_report_server', NATIVE / 'tools/soak_report_server.py')
receiver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receiver)

REPORT = (b'melee soak report v1\nresult: completed 30 minutes\ndevice: RG351P cpu=rk3326\n'
          b'cfw: AmberELEC\tx\x1b[31m\nbinary: 26d946a2e50d\nmatches: 41\n== memory ==\n== log ==\nresult: not a header\n')


class ReceiverTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name) / 'reports'
        self.server = ThreadingHTTPServer(('127.0.0.1', 0), receiver.Handler)
        self.server.store = receiver.Store(self.directory)
        threading.Thread(target=self.server.serve_forever, daemon=True).start()
        self.addCleanup(self.server.server_close)
        self.addCleanup(self.server.shutdown)
        self.url = f'http://127.0.0.1:{self.server.server_address[1]}/report'

    def post(self, body, headers=()):
        request = urllib.request.Request(self.url, data=body, headers=dict(headers), method='POST')
        try:
            with urllib.request.urlopen(request, timeout=10) as response:
                return response.status, response.read().decode()
        except urllib.error.HTTPError as error:
            return error.code, error.read().decode()

    def test_stores_a_report_and_indexes_its_header(self):
        body = gzip.compress(REPORT)
        status, answer = self.post(body)
        self.assertEqual(status, 200)
        self.assertRegex(answer, r'^ok [0-9a-f]{6}\n$')
        report_id = answer.split()[1]
        stored = [p for p in self.directory.iterdir() if p.name.endswith(f'-{report_id}.txt.gz')]
        self.assertEqual(len(stored), 1)
        self.assertEqual(stored[0].read_bytes(), body)
        line = (self.directory / 'index.tsv').read_text().splitlines()[0].split('\t')
        # Control characters in a header value never reach the index; lines after a section are ignored.
        self.assertEqual(line[1:], [report_id, 'completed 30 minutes', 'RG351P cpu=rk3326', 'AmberELEC?x?[31m',
                                    '26d946a2e50d', '41'])

    def test_rejects_what_is_not_a_report(self):
        for body in [b'plain text', gzip.compress(b'something else\n'), gzip.compress(REPORT)[:-8]]:
            self.assertEqual(self.post(body)[0], 400)
        # An oversized report is refused from its header alone; the body is never read.
        with socket.create_connection(self.server.server_address, timeout=10) as connection:
            connection.sendall(f'POST /report HTTP/1.1\r\nHost: x\r\nContent-Length: {receiver.MAX_BODY + 1}\r\n\r\n'.encode())
            self.assertTrue(connection.recv(64).startswith(b'HTTP/1.0 413'))
        self.assertFalse([p for p in self.directory.iterdir()])

    def test_nothing_is_served_back(self):
        self.post(gzip.compress(REPORT))
        name = next(p.name for p in self.directory.iterdir() if p.name.endswith('.gz'))
        for path in ['/', '/index.tsv', f'/{name}']:
            with urllib.request.urlopen(self.url.replace('/report', path), timeout=10) as response:
                self.assertEqual(response.read(), b'melee soak report receiver\n')

    def test_per_client_daily_limit(self):
        body = gzip.compress(REPORT)
        for _ in range(receiver.MAX_PER_CLIENT_PER_DAY):
            self.assertEqual(self.post(body, {'CF-Connecting-IP': '203.0.113.7'})[0], 200)
        self.assertEqual(self.post(body, {'CF-Connecting-IP': '203.0.113.7'})[0], 429)
        self.assertEqual(self.post(body, {'CF-Connecting-IP': '203.0.113.8'})[0], 200)


if __name__ == '__main__':
    unittest.main()
