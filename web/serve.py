#!/usr/bin/env python3
"""
OpenGothic Web - development server.

Serves the dist-web bundle with the cross-origin isolation headers required
for SharedArrayBuffer (COOP/COEP). Without these headers the launcher falls
back to the in-memory preload backend, which is slower and uses more RAM.

Usage:
    python3 web/serve.py [port]     # default port: 8080

For production hosting, configure the same two headers on your web server:

    nginx:
        add_header Cross-Origin-Opener-Policy same-origin;
        add_header Cross-Origin-Embedder-Policy require-corp;

    apache (.htaccess):
        Header set Cross-Origin-Opener-Policy "same-origin"
        Header set Cross-Origin-Embedder-Policy "require-corp"
"""

import sys
from functools import partial
from http.server import HTTPServer, SimpleHTTPRequestHandler
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "dist-web"


class IsolatedHandler(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
    }

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
    handler = partial(IsolatedHandler, directory=str(ROOT))
    server = HTTPServer(("127.0.0.1", port), handler)
    print(f"Serving {ROOT} at http://127.0.0.1:{port}/ (COOP/COEP enabled)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
