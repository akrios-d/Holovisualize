"""`record`: capture EMG + IMU from the Myo while you use mouse/keyboard
normally with your right arm, so `train` can later learn which muscle
patterns correspond to which actions.

Run it, do a batch of one action at a time (e.g. 20-30 left clicks, pause,
20-30 right clicks, pause, some free-form cursor movement, some space/enter
presses), then Ctrl+C. Repeat as multiple short sessions rather than one
long one — it's easier to keep gestures clean and consistent that way.
"""

from __future__ import annotations

import time

from pynput import keyboard, mouse

from .dongle import EmgSample, ImuSample, MyoDevice
from .session import SessionWriter


def run(session_name: str | None, com_port: str | None) -> None:
    writer = SessionWriter(session_name)
    print(f"Recording to {writer.dir}  (Ctrl+C to stop)")

    device = MyoDevice(com_port=com_port)

    def on_emg(s: EmgSample) -> None:
        writer.write_sensor({"t": s.t, "emg": list(s.channels)})

    def on_imu(s: ImuSample) -> None:
        writer.write_sensor(
            {"t": s.t, "quat": list(s.quat), "gyro": list(s.gyro), "accel": list(s.accel)}
        )

    def on_move(x, y) -> None:
        writer.write_event({"t": time.monotonic(), "type": "mouse_move", "x": x, "y": y})

    def on_click(x, y, button, pressed) -> None:
        writer.write_event(
            {
                "t": time.monotonic(),
                "type": "mouse_click",
                "button": str(button),
                "pressed": pressed,
            }
        )

    def on_key(key, pressed) -> None:
        # Char keys (letters, digits, ...) -> the character itself, e.g. "m".
        # Special keys (space, enter, ...) -> their name, e.g. "space", so it
        # matches the `detail` used in config bindings either way.
        if getattr(key, "char", None) is not None:
            name = key.char
        elif hasattr(key, "name"):
            name = key.name
        else:
            name = str(key)
        writer.write_event({"t": time.monotonic(), "type": "key", "name": name, "pressed": pressed})

    mouse_listener = mouse.Listener(
        on_move=on_move,
        on_click=on_click,
    )
    keyboard_listener = keyboard.Listener(
        on_press=lambda k: on_key(k, True),
        on_release=lambda k: on_key(k, False),
    )

    mouse_listener.start()
    keyboard_listener.start()
    device.start(on_emg=on_emg, on_imu=on_imu)

    try:
        while True:
            time.sleep(0.5)
    except KeyboardInterrupt:
        pass
    finally:
        device.stop()
        mouse_listener.stop()
        keyboard_listener.stop()
        writer.close()
        print(f"Saved session to {writer.dir}")
