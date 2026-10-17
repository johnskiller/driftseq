#!/usr/bin/env python3
"""http.server with a POST sink: crash beacons from the emu land in web/crash.log.

Serves web/ on 0.0.0.0:8931 (LAN + localhost port-forward) and accepts
POST /crash-beacon  ->  appends body to crash.log next to this script.
Everything else behaves like `python3 -m http.server`.
"""
import datetime, sys
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler

LOG = 'crash.log'

class H(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')   # dev server: always fresh emu.js
        super().end_headers()

    def do_POST(self):
        if self.path != '/crash-beacon':
            self.send_error(404); return
        n = int(self.headers.get('Content-Length') or 0)
        body = self.rfile.read(n).decode('utf-8', 'replace')
        with open(LOG, 'a') as f:
            f.write(body if body.endswith('\n') else body + '\n')
        self.send_response(204); self.end_headers()
        sys.stderr.write('[crash] ' + body.strip()[:200] + '\n')

    def log_message(self, fmt, *args):   # keep default access log
        sys.stderr.write("%s - - [%s] %s\n" % (
            self.address_string(), self.log_date_time_string(), fmt % args))


if __name__ == '__main__':
    ThreadingHTTPServer(('0.0.0.0', 8931), H).serve_forever()
