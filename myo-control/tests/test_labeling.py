import numpy as np

from myo_control.config import Config
from myo_control.labeling import (
    SessionArrays,
    build_cursor_dataset,
    build_gesture_dataset,
    trigger_times,
)


def make_session_with_click_at(t_click: float) -> SessionArrays:
    emg_t = np.arange(0, 2, 0.005)  # 200 Hz
    rng = np.random.default_rng(0)
    emg = rng.integers(-5, 5, size=(len(emg_t), 8)).astype(np.float64)
    # Inject an obvious burst right before the click so it's separable from rest.
    burst_mask = (emg_t > t_click - 0.1) & (emg_t <= t_click)
    emg[burst_mask] += 100
    events = [{"t": t_click, "type": "mouse_click", "button": "Button.left", "pressed": True}]
    return SessionArrays(emg_t=emg_t, emg=emg, imu_t=np.array([]), gyro=np.zeros((0, 3)), events=events)


def test_trigger_times_matches_left_click():
    config = Config()
    binding = next(b for b in config.bindings if b["label"] == "click_left")
    from myo_control.config import KeyBinding

    sess = make_session_with_click_at(1.0)
    times = trigger_times(sess.events, KeyBinding(**binding))
    assert times == [1.0]


def test_build_gesture_dataset_has_positive_and_rest_labels():
    config = Config()
    sess = make_session_with_click_at(1.0)
    X, y = build_gesture_dataset([sess], config)
    assert X.shape[0] == len(y)
    assert "click_left" in y
    assert "rest" in y


def test_build_cursor_dataset_pairs_gyro_with_deltas():
    config = Config()
    imu_t = np.arange(0, 1, 0.02)
    gyro = np.ones((len(imu_t), 3)) * 5.0
    events = [
        {"t": 0.0, "type": "mouse_move", "x": 0, "y": 0},
        {"t": 0.5, "type": "mouse_move", "x": 100, "y": 0},
        {"t": 0.9, "type": "mouse_move", "x": 100, "y": 50},
    ]
    sess = SessionArrays(emg_t=np.array([]), emg=np.zeros((0, 8)), imu_t=imu_t, gyro=gyro, events=events)
    X, Y = build_cursor_dataset([sess], config, tick_s=0.05)
    assert X.shape[1] == 3
    assert Y.shape[1] == 2
    assert X.shape[0] == Y.shape[0]
