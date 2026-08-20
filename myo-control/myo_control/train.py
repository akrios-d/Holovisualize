"""`train`: builds labeled datasets from every recorded session and fits:

- a RandomForest classifier: EMG window features -> discrete gesture label
- a Ridge regression: IMU gyro reading -> mouse (dx, dy)

Both are saved to models/ for `control.py` to load.
"""

from __future__ import annotations

from pathlib import Path

import joblib
import numpy as np
from sklearn.ensemble import RandomForestClassifier
from sklearn.linear_model import Ridge
from sklearn.model_selection import train_test_split

from .config import Config
from .labeling import build_cursor_dataset, build_gesture_dataset, load_session
from .session import SESSIONS_DIR, list_sessions

MODELS_DIR = Path("models")


def run(session_names: list[str] | None) -> None:
    config = Config.load()
    names = session_names or list_sessions()
    if not names:
        print("No recorded sessions found under data/sessions/. Run `record` first.")
        return

    sessions = [load_session(SESSIONS_DIR / name) for name in names]
    print(f"Loaded {len(sessions)} session(s): {', '.join(names)}")

    X, y = build_gesture_dataset(sessions, config)
    print(f"Gesture dataset: {X.shape[0]} windows, classes: {sorted(set(y.tolist()))}")
    gesture_model = None
    if len(set(y.tolist())) >= 2:
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=0.2, random_state=0, stratify=y
        )
        gesture_model = RandomForestClassifier(n_estimators=200, random_state=0)
        gesture_model.fit(X_train, y_train)
        acc = gesture_model.score(X_test, y_test)
        print(f"Gesture classifier held-out accuracy: {acc:.2f}")
    else:
        print("Not enough distinct gestures recorded yet to train a classifier.")

    Xg, Yg = build_cursor_dataset(sessions, config)
    print(f"Cursor dataset: {Xg.shape[0]} ticks")
    cursor_model = None
    if Xg.shape[0] >= 20:
        cursor_model = Ridge(alpha=1.0)
        cursor_model.fit(Xg, Yg)
        r2 = cursor_model.score(Xg, Yg)
        print(f"Cursor regression R^2 (train set): {r2:.2f}")
    else:
        print("Not enough free-form mouse-movement data to train cursor mapping.")

    MODELS_DIR.mkdir(exist_ok=True)
    if gesture_model is not None:
        joblib.dump(gesture_model, MODELS_DIR / "gesture_model.joblib")
        print(f"Saved {MODELS_DIR / 'gesture_model.joblib'}")
    if cursor_model is not None:
        joblib.dump(cursor_model, MODELS_DIR / "cursor_model.joblib")
        print(f"Saved {MODELS_DIR / 'cursor_model.joblib'}")
