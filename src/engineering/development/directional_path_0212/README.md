# 0.21.2 Direction-local measured path

Preserves strict global microphone calibration. New UART command CALIBRATE_PATH8 estimates a room/source/array path at one known angle, not microphone-only correction. First32 FFT frames train; next independent32 validate all8 channels (coherence>=0.9, finite relative amplitude0.125..8, relative phase change<=15deg, amplitude ratio0.8..1.25). Commit requires>=12 bins spanning>=1500Hz. Old path retained on rejection.

A committed path is normalized to energy8 and used only within15deg of measured direction, with local geometric angle increments. Outside this sector geometric steering remains. Optional LCMV measured-path null requires target separation>=45deg and measured path explains>=50% current covariance energy; must pass target-sector +/-1dB and white-noise<=4xDAS guards. Other bins retain loaded MPDR/geometric fallback.

cal_mode=0 strict global channel calibration; cal_mode=1 local path. cal_stage=1 train,2 held-out validation,3 done (strict mode stage may stay1). In mode1 diagnostic flags bit8 means held-out repeatability, max_phase_deg is segment-to-segment discrepancy, not geometric residual. cal_phase_bins is not used for mode1. path_bins/path_angle refer last committed profile. RAM-only; no startup persistence. Calibration and 30dB physical acceptance pending.
