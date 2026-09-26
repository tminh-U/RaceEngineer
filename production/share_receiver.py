"""Small RaceEngineer race-log receiver. Put HTTPS reverse proxy in front of it."""

import hashlib
import hmac
import json
import math
import os
import re
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path

MAX_BYTES = 20 * 1024 * 1024
ALLOWED = {
    "share_schema_version", "record_type", "session_id", "captured_utc",
    "simulator", "completed_lap", "lap_time_s", "sample_current_lap",
    "in_pit_at_sample", "lap_excluded", "track", "car_model",
    "car_category", "car_subclass", "total_laps", "fuel_used_l",
    "fuel_at_sample_l", "gap_ahead_at_sample_s", "gap_behind_at_sample_s",
    "current_lap", "pit_state", "realism_confirmed", "tyre_wear_0_at_sample",
    "tyre_wear_1_at_sample", "tyre_wear_2_at_sample", "tyre_wear_3_at_sample",
    "tyre_temp_0_at_sample", "tyre_temp_1_at_sample",
    "tyre_temp_2_at_sample", "tyre_temp_3_at_sample",
}
RECORD_TYPES = {"lap", "pit_enter", "pit_exit", "pit_box_enter", "pit_box_exit"}


def valid_row(row):
    if not isinstance(row, dict) or not row.keys() <= ALLOWED:
        return False
    if row.get("share_schema_version") != 1 or row.get("record_type") not in RECORD_TYPES:
        return False
    if row.get("realism_confirmed") is not True:
        return False
    if row.get("simulator") not in {"sim_ac", "sim_acc"}:
        return False
    if not isinstance(row.get("session_id"), str) or not re.fullmatch(
        r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", row["session_id"]
    ):
        return False
    if not isinstance(row.get("captured_utc"), str) or not row["captured_utc"].startswith("2000-"):
        return False
    return all(
        (isinstance(value, str) and len(value) <= 256)
        or isinstance(value, bool)
        or (isinstance(value, (int, float)) and math.isfinite(value))
        for value in row.values()
    )


class Handler(BaseHTTPRequestHandler):
    def respond(self, code, payload):
        body = json.dumps(payload, separators=(",", ":")).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        self.respond(200, {"status": "ok"}) if self.path == "/health" else self.respond(404, {})

    def do_POST(self):
        if self.path != "/v1/race-data":
            return self.respond(404, {})
        bearer = self.headers.get("Authorization", "")
        if not hmac.compare_digest(bearer, "Bearer " + self.server.upload_token):
            return self.respond(401, {"error": "unauthorized"})
        if self.headers.get_content_type() != "application/x-ndjson":
            return self.respond(415, {"error": "content type"})
        try:
            length = int(self.headers.get("Content-Length", ""))
        except ValueError:
            length = 0
        if not 0 < length <= MAX_BYTES:
            return self.respond(413, {"error": "size"})
        self.connection.settimeout(30)
        data = self.rfile.read(length)
        if len(data) != length:
            return self.respond(400, {"error": "incomplete"})
        try:
            rows = [json.loads(line) for line in data.splitlines()]
            sessions = {row["session_id"] for row in rows}
            if not rows or len(sessions) != 1 or not all(valid_row(row) for row in rows):
                raise ValueError("invalid records")
        except (UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError, ValueError):
            return self.respond(400, {"error": "invalid data"})
        digest = hashlib.sha256(data).hexdigest()
        path = self.server.data_dir / "recordings" / f"pit_strategy_laps-{digest}.jsonl"
        if not path.exists():
            pending = path.with_suffix(".tmp")
            try:
                with pending.open("wb") as output:
                    output.write(data)
                    output.flush()
                    os.fsync(output.fileno())
                os.replace(pending, path)
            except OSError:
                try:
                    pending.unlink(missing_ok=True)
                except OSError:
                    pass
                return self.respond(507, {"error": "storage"})
        self.respond(200, {"accepted": True, "id": digest})


def main():
    token = os.environ.get("RACEENGINEER_SHARE_TOKEN", "")
    if len(token) < 32:
        raise SystemExit("Set RACEENGINEER_SHARE_TOKEN to a random token of at least 32 characters")
    os.umask(0o077)
    data_dir = Path(os.environ.get("RACEENGINEER_SHARE_DIR", "./race-data"))
    (data_dir / "recordings").mkdir(parents=True, exist_ok=True)
    # ponytail: one request at a time bounds memory; use a managed upload service if traffic grows.
    server = HTTPServer(("127.0.0.1", 8087), Handler)
    server.upload_token = token
    server.data_dir = data_dir
    server.serve_forever()


if __name__ == "__main__":
    main()
