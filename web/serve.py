#!/usr/bin/env python3
"""
OpenGothic Web - development server.

Serves the dist-web bundle with the cross-origin isolation headers required
for SharedArrayBuffer (COOP/COEP). Without these headers the launcher falls
back to the in-memory preload backend, which is slower and uses more RAM.

Usage:
    python3 web/serve.py [port]               # default port: 8080, localhost only
    python3 web/serve.py 8321 --lan --https   # reachable from other devices

For production hosting, configure the same two headers on your web server:

    nginx:
        add_header Cross-Origin-Opener-Policy same-origin;
        add_header Cross-Origin-Embedder-Policy require-corp;

    apache (.htaccess):
        Header set Cross-Origin-Opener-Policy "same-origin"
        Header set Cross-Origin-Embedder-Policy "require-corp"
"""

import argparse
import socket
import ssl
import subprocess
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "dist-web"
CERT_DIR = Path(__file__).resolve().parent / ".cert"


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


def lan_addresses():
    """IPv4 addresses of this machine (for the URLs printed at startup)."""
    out = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            out.add(info[4][0])
    except OSError:
        pass
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("10.255.255.255", 1))  # no packet is sent
            out.add(s.getsockname()[0])
    except OSError:
        pass
    return sorted(a for a in out if not a.startswith("127."))


def make_cert(addresses):
    """Self-signed certificate valid for localhost and the LAN addresses."""
    cert, key = CERT_DIR / "cert.pem", CERT_DIR / "key.pem"
    CERT_DIR.mkdir(exist_ok=True)
    san = ",".join(["DNS:localhost", "IP:127.0.0.1"] + [f"IP:{a}" for a in addresses])
    stamp = CERT_DIR / "san.txt"
    if not (cert.exists() and key.exists() and stamp.exists() and stamp.read_text() == san):
        subprocess.run(
            ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "825",
             "-subj", "/CN=OpenGothic Web", "-addext", f"subjectAltName={san}",
             "-keyout", str(key), "-out", str(cert)],
            check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        stamp.write_text(san)
    return cert, key


def main():
    ap = argparse.ArgumentParser(description="OpenGothic Web development server")
    ap.add_argument("port", nargs="?", type=int, default=8080)
    ap.add_argument("--lan", action="store_true",
                    help="listen on all interfaces (open from other devices on the network)")
    ap.add_argument("--https", action="store_true",
                    help="serve HTTPS with a self-signed certificate (needed for WebGPU on other "
                         "devices: browsers only enable it on HTTPS or localhost)")
    args = ap.parse_args()

    host = "0.0.0.0" if args.lan else "127.0.0.1"
    handler = partial(IsolatedHandler, directory=str(ROOT))
    server = ThreadingHTTPServer((host, args.port), handler)
    addrs = lan_addresses()
    scheme = "http"
    if args.https:
        cert, key = make_cert(addrs)
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(cert, key)
        server.socket = ctx.wrap_socket(server.socket, server_side=True)
        scheme = "https"
    print(f"Serving {ROOT} (COOP/COEP enabled)")
    print(f"  {scheme}://127.0.0.1:{args.port}/")
    if args.lan:
        for a in addrs:
            print(f"  {scheme}://{a}:{args.port}/")
        if not args.https:
            print("Note: WebGPU, OPFS and SharedArrayBuffer are unavailable on plain http:// "
                  "addresses other than localhost; use --https.")
        else:
            print("The browser will warn about the self-signed certificate: choose Advanced, "
                  "then Proceed.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
