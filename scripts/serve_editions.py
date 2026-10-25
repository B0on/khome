#!/usr/bin/env python3
"""Serve packed newspaper editions over plain HTTP/1.1 on the LAN."""

from __future__ import annotations

import argparse
import functools
import http.server
import socket
import socketserver
import sys
from pathlib import Path


class EditionHTTPRequestHandler(http.server.SimpleHTTPRequestHandler):
    """Static file server rooted at the editions directory."""

    def __init__(self, *args: object, directory: str | None = None, **kwargs: object) -> None:
        super().__init__(*args, directory=directory, **kwargs)  # type: ignore[misc]

    def log_message(self, format: str, *args: object) -> None:
        sys.stderr.write("%s - [%s] %s\n" % (self.address_string(), self.log_date_time_string(), format % args))

    # Kindle wget is old; avoid features that confuse it.
    protocol_version = "HTTP/1.1"

    def end_headers(self) -> None:
        self.send_header("Connection", "close")
        super().end_headers()


def main() -> int:
    parser = argparse.ArgumentParser(description="Serve out/editions over HTTP/1.1")
    parser.add_argument(
        "--root",
        type=Path,
        default=Path("out/editions"),
        help="Directory containing latest-text.json and text-* folders",
    )
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: all interfaces)")
    parser.add_argument("--port", type=int, default=8088, help="TCP port (default: 8088)")
    args = parser.parse_args()

    root = args.root.resolve()
    if not root.is_dir():
        print(f"error: editions root not found: {root}", file=sys.stderr)
        return 1
    if not (root / "latest-text.json").is_file():
        print(f"warning: {root / 'latest-text.json'} is missing", file=sys.stderr)

    handler = functools.partial(EditionHTTPRequestHandler, directory=str(root))
    socketserver.TCPServer.allow_reuse_address = True

    with socketserver.TCPServer((args.host, args.port), handler) as httpd:
        host_out = args.host
        if host_out in ("0.0.0.0", ""):
            try:
                host_out = socket.gethostbyname(socket.gethostname())
            except OSError:
                host_out = "127.0.0.1"
        print(f"Serving {root} at http://{host_out}:{args.port}/")
        print(f"Kindle pointer: http://{host_out}:{args.port}/latest-text.json")
        print("Press Ctrl+C to stop.")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nStopped.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
