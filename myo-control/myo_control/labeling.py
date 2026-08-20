"""Turns a recorded session (raw sensor stream + raw input events) into
labeled training examples.

Two datasets come out of every session:

- **Gesture dataset**: EMG windows labeled with a discrete action. A
  window is a positive example for a binding when it ends right at a
  moment that binding's trigger fired (e.g. a left-click press, or the
  `enter` key going down); windows far from every trigger are sampled as
  "rest" negatives.

- **Cursor dataset**: (gyro reading -> mouse dx,dy) pairs, built by
  resampling mouse position at a fixed tick and pairing each tick's
  movement with the IMU gyro sample nearest its start. This is what
  teaches the regression model in `train.py` to turn forearm rotation
  into cursor movement.

Note on the "clutch" binding: it has no natural mouse/keyboard event of
its own (it's a gesture you invent to toggle cursor-control mode), so
during recording you mark it by holding down `binding.detail` (a
designated key, e.g. "m") while performing the gesture — see README.
"""

from __future__ import annotations

import random
from dataclasses import dataclass

import numpy as np

from .config import Config, KeyBinding
from .features import emg_window_features, sliding_windows
from .session import read_jsonl


@dataclass
class SessionArrays:
    emg_t: np.ndarray  # (n,)
    emg: np.ndarray  # (n, 8)
    imu_t: np.ndarray  # (m,)
    gyro: np.ndarray  # (m, 3)
    events: list[dict]


def load_session(session_dir) -> SessionArrays:
    emg_t, emg, imu_t, gyro = [], [], [], []
    for rec in read_jsonl(session_dir / "sensor.jsonl"):
        if "emg" in rec:
            emg_t.append(rec["t"])
            emg.append(rec["emg"])
        elif "gyro" in rec:
            imu_t.append(rec["t"])
            gyro.append(rec["gyro"])
    events = list(read_jsonl(session_dir / "events.jsonl"))
    return SessionArrays(
        emg_t=np.array(emg_t),
        emg=np.array(emg, dtype=np.float64).reshape(-1, 8),
        imu_t=np.array(imu_t),
        gyro=np.array(gyro, dtype=np.float64).reshape(-1, 3),
        events=events,
    )


def trigger_times(events: list[dict], binding: KeyBinding) -> list[float]:
    """Timestamps a binding's recording trigger fired at."""
    times = []
    for rec in events:
        if not rec.get("pressed"):
            continue
        if binding.action == "mouse_click" and rec.get("type") == "mouse_click":
            if binding.detail in str(rec.get("button", "")):
                times.append(rec["t"])
        elif binding.action in ("key_press", "clutch") and rec.get("type") == "key":
            if rec.get("name") == binding.detail:
                times.append(rec["t"])
    return times


def build_gesture_dataset(sessions: list[SessionArrays], config: Config, rest_ratio: float = 1.0):
    """Returns (X, y) for the discrete gesture classifier."""
    window_s = config.window_ms / 1000.0
    step_s = config.step_ms / 1000.0
    bindings = {b["label"]: KeyBinding(**b) for b in config.bindings}

    X, y = [], []
    for sess in sessions:
        if len(sess.emg_t) == 0:
            continue
        all_trigger_times: list[float] = []
        for label, binding in bindings.items():
            times = trigger_times(sess.events, binding)
            all_trigger_times.extend(times)
            for t in times:
                mask = (sess.emg_t > t - window_s) & (sess.emg_t <= t)
                window = sess.emg[mask]
                if len(window) >= 4:
                    X.append(emg_window_features(window))
                    y.append(label)

        # "rest" negatives: windows whose end is far from every trigger.
        n_positive = len(y)
        rest_candidates = []
        for window, t_end in sliding_windows(sess.emg, sess.emg_t, window_s, step_s):
            if len(window) < 4:
                continue
            if all(abs(t_end - trig) > window_s for trig in all_trigger_times):
                rest_candidates.append(window)
        n_rest = min(len(rest_candidates), max(1, int(n_positive * rest_ratio)))
        for window in random.sample(rest_candidates, n_rest) if rest_candidates else []:
            X.append(emg_window_features(window))
            y.append("rest")

    if not X:
        return np.empty((0, 32)), np.array([])
    return np.stack(X), np.array(y)


def build_cursor_dataset(sessions: list[SessionArrays], config: Config, tick_s: float = 0.05):
    """Returns (X, Y): gyro readings -> (dx, dy) mouse deltas per tick."""
    X, Y = [], []
    for sess in sessions:
        moves = [e for e in sess.events if e.get("type") == "mouse_move"]
        if len(moves) < 2 or len(sess.imu_t) == 0:
            continue
        moves.sort(key=lambda e: e["t"])
        t0, t1 = moves[0]["t"], moves[-1]["t"]
        tick = t0
        idx = 0
        last_x, last_y = moves[0]["x"], moves[0]["y"]
        while tick + tick_s <= t1:
            # advance to the last move at-or-before this tick boundary
            while idx + 1 < len(moves) and moves[idx + 1]["t"] <= tick:
                idx += 1
            x, y_ = moves[idx]["x"], moves[idx]["y"]
            dx, dy = x - last_x, y_ - last_y
            last_x, last_y = x, y_

            gyro_idx = int(np.argmin(np.abs(sess.imu_t - tick)))
            if abs(sess.imu_t[gyro_idx] - tick) < tick_s:
                X.append(sess.gyro[gyro_idx])
                Y.append([dx, dy])
            tick += tick_s

    if not X:
        return np.empty((0, 3)), np.empty((0, 2))
    return np.stack(X), np.stack(Y)
