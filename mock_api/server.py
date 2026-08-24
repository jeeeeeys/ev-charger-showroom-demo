#!/usr/bin/env python3
"""Minimal local GET endpoint for the showroom charger firmware."""

from __future__ import annotations

import argparse
import json
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse


COMMAND_FILE = Path(__file__).with_name("command.json")
EXPECTED_PATH = "/api/v1/chargers/EVSE-01/command"


class CommandHandler(BaseHTTPRequestHandler):
    server_version = "EVChargerMock/1.0"

    def do_GET(self) -> None:  # noqa: N802 - required by BaseHTTPRequestHandler
        if urlparse(self.path).path != EXPECTED_PATH:
            self._send_json(
                HTTPStatus.NOT_FOUND,
                {"error": "not_found", "expected_path": EXPECTED_PATH},
            )
            return

        try:
            command = json.loads(COMMAND_FILE.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            self._send_json(
                HTTPStatus.INTERNAL_SERVER_ERROR,
                {"error": "invalid_command_file", "detail": str(error)},
            )
            return

        self._send_json(HTTPStatus.OK, command)

    def log_message(self, format: str, *args: object) -> None:
        print(f"{self.client_address[0]} - {format % args}")

    def _send_json(self, status: HTTPStatus, body: object) -> None:
        encoded = json.dumps(body, separators=(",", ":")).encode("utf-8")
        self.send_response(status.value)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8080)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    server = ThreadingHTTPServer((args.host, args.port), CommandHandler)
    print(f"Mock API listening on http://{args.host}:{args.port}{EXPECTED_PATH}")
    print(f"Edit {COMMAND_FILE} to change the returned command.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping mock API.")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
