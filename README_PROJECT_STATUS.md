# Melvin AI Robot - Project Status & Specifications

## 🚀 Achievements So Far

1.  **Firmware Architecture:** Successfully migrated to a robust architecture for the **SpotPear ESP32-S3-LCD-1.54 V2.0** board using the Arduino framework within PlatformIO.
2.  **Audio System (Low-Level):** Implemented a reliable audio engine using raw ESP-IDF `driver/i2s_std.h` APIs. This bypasses the timing issues found in common libraries and ensures proper MCLK/Codec synchronization.
3.  **Codec Integration:** Full control over the **ES8311** codec via I2C, including volume management and DAC/ADC routing.
4.  **Voice Activity Detection (VAD):** Integrated ESP-SR VAD to automatically detect human speech and trigger recording without button presses.
5.  **LLM Brain:** Created a unified `MelvinAgent` capable of talking to Gemini, Groq, or OpenRouter.
6.  **Text-To-Speech (TTS):** Integrated Google TTS API to generate high-quality speech. The robot downloads audio in `LINEAR16` format and saves it as a `.wav` file on the SD card for playback.
7.  **SD Card Storage:** Configured 1-bit SDMMC mode for reliable data access (config, audio assets, and recordings).
8.  **Personalities:** Implemented a system where Melvin can act as "Rick" (cynical/sarcastic), "Calm" (polite helper), or "Podcast" (narrative style).
9.  **Web Interface:** Basic WebUI for configuration (WiFi, API keys).

---

## 🛠 Hardware Specifications (Pinout)

| Component | Pin / Value | Notes |
| :--- | :--- | :--- |
| **MCU** | ESP32-S3 | 16MB Flash, 2MB PSRAM (OPI) |
| **I2C SDA** | GPIO 15 | Shared for Codec, RTC, SHTC3 |
| **I2C SCL** | GPIO 14 | Shared for Codec, RTC, SHTC3 |
| **I2S MCLK** | GPIO 16 | Required for ES8311 clocking |
| **I2S BCLK** | GPIO 9 | Bit Clock |
| **I2S WS / LRCK** | GPIO 45 | Word Select |
| **I2S DOUT** | GPIO 8 | ESP32 TX -> Codec DAC |
| **I2S DIN** | GPIO 10 | Codec ADC -> ESP32 RX |
| **PA_CTRL** | GPIO 46 | Speaker Amp Control (Active HIGH) |
| **Display SCLK** | GPIO 4 | ST7789 |
| **Display MOSI** | GPIO 2 | ST7789 |
| **Display DC** | GPIO 47 | Data/Command |
| **Display CS** | GPIO 5 | Chip Select |
| **Display RST** | GPIO 38 | Reset |
| **Display BL** | GPIO 42 | Backlight (PWM) |
| **SDMMC CLK** | GPIO 17 | 1-bit mode |
| **SDMMC CMD** | GPIO 18 | 1-bit mode |
| **SDMMC D0** | GPIO 21 | 1-bit mode |
| **BOOT Button** | GPIO 0 | Built-in button |

---

## 📡 Electronic Components (BOM)

1.  **Audio Codec:** ES8311 (Low power, high quality mono DAC/ADC).
2.  **Audio Amplifier:** NS4150B (3W Mono Class D).
3.  **Microphone:** MB23 H11W (Analog MEMS, connected to ES8311).
4.  **Display:** 1.54-inch LCD (ST7789 controller, 240x240 resolution).
5.  **RTC:** PCF85063 (Real-time clock for scheduling).
6.  **Environment:** SHTC3 (Temperature and Humidity sensor).
7.  **Wireless:** ESP32-S3 internal Wi-Fi & Bluetooth 5.0 (LE).

---

## 🗣 Speech Format Requirements

To make Melvin speak, audio files must strictly adhere to the following format to be compatible with the current I2S/Codec driver:

*   **Container:** WAV (RIFF)
*   **Codec:** Uncompressed PCM (LINEAR16)
*   **Sample Rate:** **16000 Hz** (Standard for VAD/Agent loop)
*   **Bit Depth:** 16-bit
*   **Channels:** Mono (Left/Right data is mirrored by the driver)

**Python Conversion Command (ffmpeg):**
```bash
ffmpeg -i input.mp3 -ar 16000 -ac 1 -c:a pcm_s16le output.wav
```

---

## 📂 Repository Structure

*   `src/`: C++ Source code (Firmware).
*   `include/`: Header files and configuration templates.
*   `lib/`: Custom or modified libraries.
*   `wav_files/`: Source audio assets.
*   `python416/`: Main project directory for PlatformIO.
*   `plans/`: Architectural designs and future goals.
