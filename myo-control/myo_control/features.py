"""Sliding-window feature extraction from raw EMG/IMU samples.

Raw 200 Hz EMG is too noisy to classify sample-by-sample; standard EMG
gesture-recognition practice is to compute time-domain features over a
short sliding window (here 200 ms, hopping every 50 ms) — see e.g. Phinyomark
et al. on EMG feature sets. We use four cheap, well-established ones per
channel: MAV, RMS, waveform length and zero-crossing count.
"""

from __future__ import annotations

import numpy as np

N_CHANNELS = 8
FEATURES_PER_CHANNEL = 4  # MAV, RMS, WL, ZC


def emg_window_features(window: np.ndarray) -> np.ndarray:
    """window: shape (n_samples, 8) raw EMG int8 values -> flat feature vector."""
    x = window.astype(np.float64)
    mav = np.mean(np.abs(x), axis=0)
    rms = np.sqrt(np.mean(x**2, axis=0))
    wl = np.sum(np.abs(np.diff(x, axis=0)), axis=0)
    zc = np.sum(np.diff(np.sign(x), axis=0) != 0, axis=0)
    return np.concatenate([mav, rms, wl, zc])


def sliding_windows(samples: np.ndarray, timestamps: np.ndarray, window_s: float, step_s: float):
    """Yields (window_samples, window_end_timestamp) for a stream of
    (t, values) pairs sorted by time."""
    if len(timestamps) == 0:
        return
    t0, t_end = timestamps[0], timestamps[-1]
    start = t0
    while start + window_s <= t_end:
        stop = start + window_s
        mask = (timestamps >= start) & (timestamps < stop)
        if np.any(mask):
            yield samples[mask], stop
        start += step_s


def gyro_magnitude(gyro: tuple[float, float, float]) -> float:
    return float(np.linalg.norm(gyro))
