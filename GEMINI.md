# Melvin Project - Technical Lessons & Fixes

## Audio System (ES8311 Codec)

### THE ARCHITECTURAL SHIFT (v6.3+)
**Rule:** DO NOT use the `ESP32-audioI2S` (`Audio.h`) library. It causes MCLK timing issues and codec silence on the SpotPear board.
**Standard:** Use raw ESP-IDF `driver/i2s_std.h` APIs for I2S.
**Reason:** Direct control over I2S channels and the ES8311 I2C configuration is required to ensure MCLK is active BEFORE codec initialization and to handle the PA strapping pin (GPIO46) correctly.

### ES8311 Register Settings (Raw I2S Mode)
To ensure the codec works with `i2s_std.h`:
1.  **Reg 0x01 = 0x3F**: MCLK/LRCK divider.
2.  **Reg 0x32 = 0xBF**: Analog Volume.
3.  **Reg 0x37 = 0x08**: DAC to Output Mixer routing.
4.  **Reg 0x45 = 0x22**: Output driver gain.

## Hardware Constraints
- **PA_CTRL (GPIO46)**: This is a strapping pin. Initialize as OUTPUT but keep LOW during boot. Only drive HIGH immediately before audio playback and drive LOW after playback to prevent static hiss.
- **MicroSD (SDMMC)**: Use 1-bit mode to avoid conflicts with other pins.
---
