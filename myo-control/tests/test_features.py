import numpy as np

from myo_control.features import emg_window_features, sliding_windows


def test_emg_window_features_shape():
    window = np.random.randint(-128, 127, size=(40, 8))
    feats = emg_window_features(window)
    assert feats.shape == (32,)  # 8 channels x 4 features


def test_emg_window_features_constant_signal_has_zero_wl_and_zc():
    window = np.full((40, 8), 10)
    feats = emg_window_features(window)
    wl = feats[16:24]
    zc = feats[24:32]
    assert np.allclose(wl, 0)
    assert np.allclose(zc, 0)


def test_sliding_windows_covers_expected_range():
    t = np.arange(0, 2, 0.005)  # 200 Hz for 2s
    x = np.zeros((len(t), 8))
    windows = list(sliding_windows(x, t, window_s=0.2, step_s=0.05))
    assert len(windows) > 0
    for w, t_end in windows:
        assert w.shape[1] == 8
        assert t_end <= t[-1] + 0.2
