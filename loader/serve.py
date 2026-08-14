from __future__ import annotations

import argparse
import http.server
import json
import re
import socket
import socketserver
import sys
import threading
import webbrowser
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import desmos_data

WEB_ROOT = Path(__file__).resolve().parent / "web"

_LIST_PREVIEW = 6
_LIST_RE = re.compile(r"^([A-Za-z](?:_\{[^}]*\})?)=\[([-\d.,eE+\s]*)\]$")

def summarise_expressions(text: str) -> list[dict]:
    rows: list[dict] = []
    polygons = 0

    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue

        row: dict = {"text": line, "kind": "def"}
        m = _LIST_RE.match(line)
        if m:
            values = [v for v in m.group(2).split(",") if v.strip()]
            if len(values) > _LIST_PREVIEW:
                head = ",".join(values[:_LIST_PREVIEW])
                row["text"] = f"{m.group(1)}=[{head},...]"
                row["note"] = f"{len(values):,} values"
            row["kind"] = "list"
        elif line.startswith("polygon("):
            polygons += 1

            row["kind"] = "backdrop" if polygons == 1 else "regions"
        elif line.startswith("f="):
            row["kind"] = "slider"

        rows.append(row)
    return rows

class Viewer:

    def __init__(self, path: str | Path) -> None:
        self.anim = desmos_data.load(path)
        data, starts = self.anim.flatten()

        self.data = data.astype(np.float64)
        self.starts = starts.astype(np.float64)

        first = self.anim.parts[0]
        self.expressions = (
            summarise_expressions(first.path.read_text(encoding="utf-8"))
            if first.path
            else []
        )

    def meta(self) -> dict:
        counts = np.diff(self.starts).astype(int)
        return {
            "gridW": self.anim.grid_w,
            "gridH": self.anim.grid_h,
            "fps": self.anim.fps,
            "frames": self.anim.frame_count,
            "regions": int(len(self.data)),
            "maxRegions": int(counts.max()) if counts.size else 0,
            "avgRegions": float(counts.mean()) if counts.size else 0.0,
            "source": str(self.anim.source),
            "parts": [
                {
                    "index": i + 1,
                    "first": p.first_frame,
                    "count": p.frame_count,
                    "regions": int(len(p.data)),
                    "name": p.path.name if p.path else f"part {i + 1}",
                }
                for i, p in enumerate(self.anim.parts)
            ],
            "expressions": self.expressions,
        }

def make_handler(viewer: Viewer):
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory=str(WEB_ROOT), **kwargs)

        def _send_bytes(self, payload: bytes, ctype: str) -> None:
            self.send_response(200)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(payload)

        def do_GET(self):
            if self.path.startswith("/api/"):
                route = self.path.split("?", 1)[0]
                if route == "/api/meta":
                    body = json.dumps(viewer.meta()).encode("utf-8")
                    return self._send_bytes(body, "application/json")
                if route == "/api/d.bin":
                    return self._send_bytes(
                        viewer.data.tobytes(), "application/octet-stream"
                    )
                if route == "/api/s.bin":
                    return self._send_bytes(
                        viewer.starts.tobytes(), "application/octet-stream"
                    )
                self.send_error(404, "unknown API route")
                return None
            return super().do_GET()

        def log_message(self, fmt, *args):
            pass

    return Handler

class Server(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

def _bind(host: str, port: int, handler, tries: int = 20) -> Server:
    last: OSError | None = None
    for candidate in range(port, port + tries):
        try:
            return Server((host, candidate), handler)
        except OSError as exc:
            last = exc
    raise SystemExit(f"serve.py: no free port in {port}..{port + tries - 1} ({last})")

def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Serve vid2desmos output as a local, Desmos-like web viewer."
    )
    p.add_argument("path", help="output directory, or a single frames_partNN.txt")
    p.add_argument("--port", type=int, default=8000, help="preferred port (default 8000)")
    p.add_argument("--host", default="127.0.0.1", help="bind address (default 127.0.0.1)")
    p.add_argument("--no-browser", action="store_true", help="do not open a browser")
    args = p.parse_args(argv)

    if not WEB_ROOT.is_dir():
        print(f"serve.py: missing web assets at {WEB_ROOT}", file=sys.stderr)
        return 1

    try:
        viewer = Viewer(args.path)
    except (FileNotFoundError, desmos_data.ParseError) as exc:
        print(f"serve.py: {exc}", file=sys.stderr)
        return 1

    print(desmos_data.describe(viewer.anim))

    server = _bind(args.host, args.port, make_handler(viewer))
    host, port = server.server_address[0], server.server_address[1]
    url = f"http://{host}:{port}/"
    print(f"\nserving {url}   (ctrl-c to stop)")

    if not args.no_browser:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopped")
    finally:
        server.server_close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
