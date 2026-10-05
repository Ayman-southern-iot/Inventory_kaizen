#!/usr/bin/env python3
"""
Mock of the IMS inventory API (https://ims.siot.solutions), shaped to match the real
contract so the firmware can be verified before the live key is available.

Implements:
    GET /api/v1/products?search=&limit=&page=   -> {items[], page, limit, total}
    GET /api/v1/catalogue                       -> {products[], categories[], locations[]}
    GET /api/v1/products/<id>                   -> one product
    GET /health                                 -> {ok:true}

Auth: `Authorization: Bearer <key>` ONLY, as the real API requires. A missing or wrong
key returns 401 with IMS's documented error body {"code","message"} -- the failure path
has to be testable, not just the happy path.

    python tools/mock_inventory.py --host 0.0.0.0 --port 8080 --api-key ims_mock
"""

import argparse
import json
import re
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

# Catalogue skewed towards "arduino" so the demo search returns several real matches,
# including near-identical names -- the realistic case, not a convenient one.
PRODUCTS = [
    ("P-1001", "Arduino Uno R3",            "pcs", 24, 18, 6,  "A1-S2"),
    ("P-1002", "Arduino Uno R4 WiFi",       "pcs", 10,  7, 3,  "A1-S3"),
    ("P-1003", "Arduino Nano Every",        "pcs", 35, 35, 0,  "A2-S1"),
    ("P-1004", "Arduino Mega 2560",         "pcs",  6,  2, 4,  "A2-S4"),
    ("P-1005", "Arduino Nano 33 BLE",       "pcs",  0,  0, 0,  "A3-S1"),
    ("P-1006", "Arduino Shield Proto",      "pcs", 48, 44, 4,  "B1-S1"),
    ("P-2001", "ESP32-S3 DevKitC",          "pcs", 15, 12, 3,  "B2-S1"),
    ("P-2002", "ESP32-P4 WIFI6 Dev Kit",    "pcs",  4,  3, 1,  "B2-S2"),
    ("P-3001", "Raspberry Pi 5 8GB",        "pcs",  7,  5, 2,  "C1-S1"),
    ("P-4001", "Breadboard 830pt",          "pcs", 60, 58, 2,  "D1-S1"),
    ("P-4002", "Jumper Wire Set M-M",       "set", 25, 20, 5,  "D1-S2"),
    ("P-5001", "SSD1306 OLED 0.96in",       "pcs", 30, 27, 3,  "D2-S1"),
    ("P-5002", "GT-511C3 Fingerprint",      "pcs",  3,  3, 0,  "D2-S2"),
    ("P-6001", "MAX485 RS-485 Module",      "pcs", 18, 18, 0,  "E1-S1"),
]

CATEGORIES = [
    {"id": "c1", "name": "Development Boards", "path": "Electronics / Development Boards"},
    {"id": "c2", "name": "Prototyping",        "path": "Electronics / Prototyping"},
    {"id": "c3", "name": "Displays",           "path": "Electronics / Displays"},
]
LOCATIONS = [
    {"id": "l1", "zone": "A", "compartments": ["A1-S2", "A1-S3", "A2-S1", "A2-S4", "A3-S1"]},
    {"id": "l2", "zone": "B", "compartments": ["B1-S1", "B2-S1", "B2-S2"]},
    {"id": "l3", "zone": "C", "compartments": ["C1-S1"]},
]


def build(idx, row):
    code, name, unit, total, available, in_use, shelf = row
    return {
        "id": f"prod_{idx:04d}",
        "code": code,
        "name": name,
        "description": f"{name} held in stores",
        "unit": unit,
        "categoryPath": CATEGORIES[idx % len(CATEGORIES)]["path"],
        # Nested exactly as the IMS docs describe: stock {total, available, inUse}
        "stock": {"total": total, "available": available, "inUse": in_use},
        "shelf": shelf,
    }


ITEMS = [build(i, r) for i, r in enumerate(PRODUCTS)]


class Handler(BaseHTTPRequestHandler):
    api_key = "ims_mock"
    require_auth = True

    def _send(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _bearer_ok(self):
        if not self.require_auth:
            return True
        auth = self.headers.get("Authorization", "")
        return auth.startswith("Bearer ") and auth[7:].strip() == self.api_key

    def do_GET(self):
        u = urlparse(self.path)
        path = u.path.rstrip("/") or "/"
        q = parse_qs(u.query)

        if path == "/health":
            self._send(200, {"ok": True, "products": len(ITEMS)})
            return

        if not self._bearer_ok():
            hdr = self.headers.get("Authorization")
            self._send(401, {
                "code": "UNAUTHENTICATED" if not hdr else "API_KEY_INVALID",
                "message": "No Authorization header sent" if not hdr
                           else "Wrong, revoked or expired key",
            })
            return

        if path == "/api/v1/catalogue":
            self._send(200, {"products": ITEMS, "categories": CATEGORIES,
                             "locations": LOCATIONS, "total": len(ITEMS)})
            return

        if path == "/api/v1/products":
            search = (q.get("search", [""])[0] or "").strip().lower()
            limit = min(int(q.get("limit", ["100"])[0] or 100), 100)
            page = max(int(q.get("page", ["1"])[0] or 1), 1)

            matched = [it for it in ITEMS
                       if not search
                       or search in it["name"].lower()
                       or search in it["code"].lower()]
            start = (page - 1) * limit
            self._send(200, {"items": matched[start:start + limit],
                             "page": page, "limit": limit, "total": len(matched)})
            return

        m = re.fullmatch(r"/api/v1/products/([A-Za-z0-9_-]+)", path)
        if m:
            for it in ITEMS:
                if it["id"] == m.group(1) or it["code"].lower() == m.group(1).lower():
                    self._send(200, it)
                    return
            self._send(404, {"code": "NOT_FOUND", "message": "No such product"})
            return

        self._send(404, {"code": "NOT_FOUND", "message": f"No route {path}"})

    def log_message(self, fmt, *args):
        print(f"  {self.address_string()} {fmt % args}", flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--api-key", default="ims_mock")
    ap.add_argument("--no-auth", action="store_true")
    a = ap.parse_args()
    Handler.api_key = a.api_key
    Handler.require_auth = not a.no_auth
    print(f"IMS mock on http://{a.host}:{a.port}  ({len(ITEMS)} products, Bearer auth"
          f"{' DISABLED' if a.no_auth else ''})", flush=True)
    ThreadingHTTPServer((a.host, a.port), Handler).serve_forever()


if __name__ == "__main__":
    main()
