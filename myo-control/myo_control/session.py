"""Recorded-session storage: one directory per take under data/sessions/.

Two append-only JSONL files per session, written live during `record`:

  sensor.jsonl  {"t": <float>, "emg": [8 ints]}                (~200 Hz)
                {"t": <float>, "quat": [4], "gyro": [3], "accel": [3]} (~50 Hz)
  events.jsonl  {"t": <float>, "type": "mouse_move", "dx": .., "dy": ..}
                {"t": <float>, "type": "mouse_click", "button": .., "pressed": bool}
                {"t": <float>, "type": "key", "name": .., "pressed": bool}

Keeping sensor and input events in separate files avoids ever blocking the
high-rate sensor thread on the input-event writer (or vice versa); `train`
aligns them by timestamp afterwards.
"""

from __future__ import annotations

import json
import time
from pathlib import Path

SESSIONS_DIR = Path("data/sessions")


class SessionWriter:
    def __init__(self, name: str | None = None):
        self.name = name or time.strftime("%Y%m%d_%H%M%S")
        self.dir = SESSIONS_DIR / self.name
        self.dir.mkdir(parents=True, exist_ok=True)
        self._sensor_f = open(self.dir / "sensor.jsonl", "a", buffering=1)
        self._events_f = open(self.dir / "events.jsonl", "a", buffering=1)

    def write_sensor(self, record: dict) -> None:
        self._sensor_f.write(json.dumps(record) + "\n")

    def write_event(self, record: dict) -> None:
        self._events_f.write(json.dumps(record) + "\n")

    def close(self) -> None:
        self._sensor_f.close()
        self._events_f.close()


def read_jsonl(path: Path):
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


def list_sessions() -> list[str]:
    if not SESSIONS_DIR.exists():
        return []
    return sorted(p.name for p in SESSIONS_DIR.iterdir() if p.is_dir())
