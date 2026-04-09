# Xiaozhi Melvin Firmware Stabilization: Development Report

This document summarizes the persistent issues encountered during the development of the Melvin firmware (v4.1.0 to v4.1.9) and the technical solutions applied to achieve stability.

## 1. I2S Resource Leak: "No available channel found"

### The Problem
During transitions between **TX Mode** (Audio playback) and **RX Mode** (Voice recording), the system would frequently crash or become silent with the following error:
```text
E (35408) i2s_common: i2s_new_channel(1032): no available channel found
[E] Audio.cpp:5782 I2S channel: invalid argument
```
This was caused by the `ESP32-audioI2S` library not fully releasing the I2S hardware driver when `stopSong()` or `setPinout()` was called. Because the ESP32-S3 has limited I2S channels, redundant initializations quickly exhausted all available resources.

### The Solution: Atomic Re-creation Strategy
Instead of relying on the library to stop, we now **delete** the `Audio` object and re-instantiate it using `new Audio()` every time the system enters TX mode.
```cpp
// Deleting forces the destructor to unregister the I2S driver
delete audio; 
audio = NULL; 
// ...
audio = new Audio(); // Fresh initialization
audio->setPinout(I2S_BCLK_PIN, I2S_WS_PIN, I2S_DOUT_PIN, I2S_MCLK_PIN);
```

---

## 2. I2C Lock Contention: "Could not acquire lock"

### The Problem
After optimizing the boot sequence, the following errors appeared in the early boot logs:
```text
[  1917][E][Wire.cpp:424] beginTransmission(): could not acquire lock
[  1923][E][Wire.cpp:544] write(): NULL TX buffer pointer
```
This identified two critical bugs:
1. **Missing `Wire.begin()`**: The I2C bus was never officially started with the correct pins for the ES8311 codec.
2. **Task Contention**: The `animationTask` was being started so early that it conflicted with the hardware initialization of the I2C bus on Core 1.

### The Solution
We moved `Wire.begin(15, 14)` to the very start of `setup()` and re-ordered the boot sequence to ensure basic hardware buses are ready before starting high-priority background tasks like the face animation.

---

## 3. Unreliable Voice Triggers: Advanced VAD

### The Problem
The previous simple amplitude-based threshold (`if (db > -32)`) was frequently false-triggering due to background noise or "popping" from the microphone gain.

### The Solution: ZCR + Energy Detection
Integrated logic from **PR #1** to implement a robust **Voice Activity Detector (VAD)** using:
- **Zero-Crossing Rate (ZCR)**: Counting how often the signal waveform crosses the zero-axis to distinguish speech from static.
- **RMS Energy Integration**: Calculating the average energy over 2048 samples.
- **Dynamic dB Silencing**: Adjusting thresholds based on whether speech is already detected.

---

## 4. UI Stability and Boot Speed

### The Problem
The device would show a black screen for several seconds while waiting for Wi-Fi and SD card mounting.

### The Solution: Early Task Pinned to Core 0
The `animationTask` is now pinned to **Core 0** and started as soon as the LCD is initialized. This provides instant visual feedback to the user while the networking and filesystem logic proceeds independently on **Core 1**.

---

## 5. Final Hardware Configuration
- **Board**: `esp32-s3-devkitc-1`
- **Flash**: 16MB (OPI)
- **PSRAM**: 8MB (OPI)
- **Log Level**: `1` (Error only) to prevent "m_limiter" spam and timing jitter.
- **Audio Volume**: Set to **21** for clear output.

> [!TIP]
> All changes are committed to the local workspace and are ready for GitHub. Use `git push` to finalize the update.
