"""`control`: the live loop. Streams EMG+IMU from the Myo, runs the trained
models, and drives the real mouse/keyboard through pynput.

Two things run concurrently:

- **Gesture classifier** on a sliding EMG window fires discrete actions
  (click, key press) when it's confident and the per-action cooldown has
  elapsed, so a held gesture doesn't repeat-fire every window.
- **Cursor regression** turns each IMU gyro sample into a mouse delta,
  but only while "cursor-move mode" is on. It starts off; the `clutch`
  gesture toggles it, the same way lifting a physical mouse off the pad
  stops it from moving the cursor — without a clutch, ordinary arm motion
  would drag the cursor around constantly.
"""

from __future__ import annotations

import time
from collections import deque
from pathlib import Path

import joblib
import numpy as np
from pynput.keyboard import Controller as KeyboardController
from pynput.keyboard import Key
from pynput.mouse import Button, Controller as MouseController

from .config import Config, KeyBinding
from .dongle import EmgSample, ImuSample, MyoDevice
from .features import emg_window_features

MODELS_DIR = Path("models")

SPECIAL_KEYS = {
    "space": Key.space,
    "enter": Key.enter,
    "tab": Key.tab,
    "esc": Key.esc,
}


class ActionExecutor:
    def __init__(self, config: Config):
        self.config = config
        self.mouse = MouseController()
        self.keyboard = KeyboardController()
        self.cursor_move_enabled = False
        self._last_fired: dict[str, float] = {}
        self._bindings = {b["label"]: KeyBinding(**b) for b in config.bindings}

    def maybe_fire(self, label: str) -> None:
        if label == "rest":
            return
        binding = self._bindings.get(label)
        if binding is None:
            return
        now = time.monotonic()
        if now - self._last_fired.get(label, 0.0) < self.config.action_cooldown_s:
            return
        self._last_fired[label] = now

        if binding.action == "mouse_click":
            button = Button.left if binding.detail == "left" else Button.right
            self.mouse.click(button)
            print(f"[action] click {binding.detail}")
        elif binding.action == "key_press":
            key = SPECIAL_KEYS.get(binding.detail, binding.detail)
            self.keyboard.press(key)
            self.keyboard.release(key)
            print(f"[action] key {binding.detail}")
        elif binding.action == "clutch":
            self.cursor_move_enabled = not self.cursor_move_enabled
            state = "ON" if self.cursor_move_enabled else "OFF"
            print(f"[clutch] cursor movement {state}")

    def move_cursor(self, dx: float, dy: float) -> None:
        if not self.cursor_move_enabled:
            return
        cap = self.config.cursor_max_pixels_per_tick
        dx = max(-cap, min(cap, dx))
        dy = max(-cap, min(cap, dy))
        self.mouse.move(dx, dy)


def run(com_port: str | None) -> None:
    config = Config.load()
    executor = ActionExecutor(config)

    gesture_model = None
    cursor_model = None
    gm_path, cm_path = MODELS_DIR / "gesture_model.joblib", MODELS_DIR / "cursor_model.joblib"
    if gm_path.exists():
        gesture_model = joblib.load(gm_path)
        print(f"Loaded gesture model from {gm_path}")
    else:
        print("No gesture model found — discrete actions disabled. Run `train` first.")
    if cm_path.exists():
        cursor_model = joblib.load(cm_path)
        print(f"Loaded cursor model from {cm_path}")
    else:
        print("No cursor model found — cursor movement disabled. Run `train` first.")

    window_s = config.window_ms / 1000.0
    buffer: deque[EmgSample] = deque()

    def on_emg(s: EmgSample) -> None:
        buffer.append(s)
        while buffer and s.t - buffer[0].t > window_s:
            buffer.popleft()
        if gesture_model is None or len(buffer) < 4:
            return
        window = np.array([b.channels for b in buffer], dtype=np.float64)
        feats = emg_window_features(window).reshape(1, -1)
        probs = gesture_model.predict_proba(feats)[0]
        classes = gesture_model.classes_
        best_idx = int(np.argmax(probs))
        if probs[best_idx] >= config.confidence_threshold:
            executor.maybe_fire(classes[best_idx])

    def on_imu(s: ImuSample) -> None:
        if cursor_model is None:
            return
        gyro = np.array(s.gyro).reshape(1, -1)
        if np.linalg.norm(s.gyro) < config.cursor_deadzone_dps:
            return
        dx, dy = cursor_model.predict(gyro)[0]
        executor.move_cursor(dx, dy)

    device = MyoDevice(com_port=com_port)
    device.start(on_emg=on_emg, on_imu=on_imu)
    print("Control loop running. Ctrl+C to stop.")

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        device.stop()
