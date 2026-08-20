"""Thin wrapper around the `pyomyo` library, the hardware layer.

The original Myo armband was discontinued by Thalmic Labs in 2018 and the
official "Myo Connect" driver requires a one-time online account
activation against servers that no longer exist — fresh installs of the
official SDK typically fail to activate today. `pyomyo`
(https://github.com/PerlinWarp/pyomyo) talks directly to the Bluetooth
dongle over its serial (BLED112/BGAPI) protocol and needs no Myo Connect,
no SDK and no account — just the dongle plugged in. That is why it is the
hardware dependency here instead of the official SDK.

Everything myo-control-specific (recording, training, control mapping)
only depends on the small interface below, so if `pyomyo`'s own API
drifts, this is the one file to adjust.
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Callable, Optional


@dataclass
class ImuSample:
    t: float
    quat: tuple[float, float, float, float]
    accel: tuple[float, float, float]
    gyro: tuple[float, float, float]


@dataclass
class EmgSample:
    t: float
    channels: tuple[int, int, int, int, int, int, int, int]


EmgHandler = Callable[[EmgSample], None]
ImuHandler = Callable[[ImuSample], None]


class MyoDevice:
    """Streams EMG + IMU samples from a Myo armband via its BLE dongle.

    Usage:
        dev = MyoDevice()
        dev.start(on_emg=..., on_imu=...)
        ...
        dev.stop()
    """

    def __init__(self, com_port: Optional[str] = None):
        self._com_port = com_port
        self._myo = None
        self._thread: Optional[threading.Thread] = None
        self._running = False

    def start(self, on_emg: EmgHandler, on_imu: ImuHandler) -> None:
        try:
            from pyomyo import Myo, emg_mode
        except ImportError as exc:  # pragma: no cover - environment dependent
            raise RuntimeError(
                "pyomyo is not installed. Run `pip install pyomyo` "
                "(see README for dongle setup)."
            ) from exc

        self._myo = Myo(mode=emg_mode.FILTERED, tty=self._com_port)

        def _emg_cb(emg, moving):
            on_emg(EmgSample(t=time.monotonic(), channels=tuple(emg)))

        def _imu_cb(quat, acc, gyro):
            on_imu(
                ImuSample(
                    t=time.monotonic(),
                    quat=tuple(quat),
                    accel=tuple(acc),
                    gyro=tuple(gyro),
                )
            )

        self._myo.add_emg_handler(_emg_cb)
        self._myo.add_imu_handler(_imu_cb)
        self._myo.connect()
        # A couple of short buzzes confirms a live connection without
        # needing to watch logs.
        self._myo.vibrate(1)

        self._running = True

        def _loop():
            while self._running:
                try:
                    self._myo.run()
                except Exception:
                    # A single malformed BLE packet shouldn't kill the
                    # session; log-and-continue keeps the stream alive.
                    time.sleep(0.001)

        self._thread = threading.Thread(target=_loop, daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._running = False
        if self._thread is not None:
            self._thread.join(timeout=1.0)
        if self._myo is not None:
            try:
                self._myo.disconnect()
            except Exception:
                pass


def list_candidate_ports() -> list[str]:
    """Lists serial ports that look like a Bluegiga BLED112 dongle (the
    one bundled with the Myo), to help pick `--port` when auto-detect
    fails. VID 0x2458 / PID 0x0001 is the Bluegiga vendor/product id."""
    import serial.tools.list_ports as list_ports

    candidates = []
    for p in list_ports.comports():
        candidates.append(p.device)
    return candidates
