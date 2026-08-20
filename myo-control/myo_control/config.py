"""Default configuration and persistence for myo-control.

All tunables (which gestures map to which mouse/keyboard actions, sample
rates, window sizes, sensitivities) live here so `record`, `train` and
`control` agree on the same values without duplicating literals.
"""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

CONFIG_PATH = Path("myo_control_config.json")

# EMG streams at ~200 Hz, IMU at ~50 Hz on the Myo. Feature windows are
# computed over the EMG stream; the most recent IMU sample at window-end
# is used for the cursor-movement regression.
EMG_SAMPLE_RATE_HZ = 200
WINDOW_MS = 200
STEP_MS = 50  # hop between successive windows (sliding window, overlapping)

# Discrete actions the gesture classifier can fire. "rest" must stay first:
# it is the implicit "do nothing" class and never bound to an OS action.
GESTURE_LABELS = [
    "rest",
    "click_left",
    "click_right",
    "key_space",
    "key_enter",
    "clutch",  # toggles cursor-movement mode on/off
]

# Minimum classifier confidence to act on a prediction, and the cooldown
# (seconds) before the same discrete action can fire again — without this
# a held gesture would repeat-fire once per window.
CONFIDENCE_THRESHOLD = 0.7
ACTION_COOLDOWN_S = 0.35

# Cursor mapping: gyro (deg/s, from IMU) -> screen-pixel delta per control
# tick. Learned as a linear regression during `train`, but these are the
# fallback/starting gains used before any model exists and to clamp output.
CURSOR_GAIN_XY = 4.0
CURSOR_DEADZONE_DPS = 3.0
CURSOR_MAX_PIXELS_PER_TICK = 40


@dataclass
class KeyBinding:
    """Maps a discrete gesture label to a pynput action."""

    label: str
    action: str  # "mouse_click", "key_press", "clutch"
    detail: str = ""  # e.g. "left" / "right" for clicks, "space" for keys


DEFAULT_BINDINGS = [
    KeyBinding("click_left", "mouse_click", "left"),
    KeyBinding("click_right", "mouse_click", "right"),
    KeyBinding("key_space", "key_press", "space"),
    KeyBinding("key_enter", "key_press", "enter"),
    KeyBinding("clutch", "clutch", ""),
]


@dataclass
class Config:
    emg_sample_rate_hz: int = EMG_SAMPLE_RATE_HZ
    window_ms: int = WINDOW_MS
    step_ms: int = STEP_MS
    gesture_labels: list[str] = field(default_factory=lambda: list(GESTURE_LABELS))
    confidence_threshold: float = CONFIDENCE_THRESHOLD
    action_cooldown_s: float = ACTION_COOLDOWN_S
    cursor_gain_xy: float = CURSOR_GAIN_XY
    cursor_deadzone_dps: float = CURSOR_DEADZONE_DPS
    cursor_max_pixels_per_tick: int = CURSOR_MAX_PIXELS_PER_TICK
    bindings: list[dict] = field(
        default_factory=lambda: [asdict(b) for b in DEFAULT_BINDINGS]
    )

    @staticmethod
    def load(path: Path = CONFIG_PATH) -> "Config":
        if not path.exists():
            return Config()
        data = json.loads(path.read_text())
        return Config(**data)

    def save(self, path: Path = CONFIG_PATH) -> None:
        path.write_text(json.dumps(asdict(self), indent=2))
