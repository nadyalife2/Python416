#include <Arduino.h>
#include "Audio.h"
#include <SD_MMC.h>
#include "MelvinTTS.h"
#include <Wire.h>
#include "Display.h"

// ============================================================
// SpotPear ESP32-S3-1.54 V2.0 — THE MCLK SECRET (v6.1)
// ============================================================

LGFX lcd;

#define I2C_SCL_PIN    14
#define I2C_SDA_PIN    15
#define ES8311_ADDR    0x18

#define I2S_MCLK_PIN   16
#define I2S_BCLK_PIN   9
#define I2S_LRCK_PIN   45
#define I2S_DOUT_PIN   8 
#define I2S_DIN_PIN    10

#define SD_CLK_PIN     17
#define SD_CMD_PIN     18
#define SD_D0_PIN      21

#define PA_CTRL_PIN    46
#define BOOT_BUTTON_PIN 0

Audio audio;
MelvinTTS tts;
bool sdCardInitialized = false;

void showStatus(const char* msg, uint16_t color = TFT_WHITE) {
  Serial.println(msg);
  lcd.setTextColor(color);
  lcd.println(msg);
}

static bool es_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg); Wire.write(val);
  return (Wire.endTransmission() == 0);
}

uint8_t es_read(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

void initCodec() {
  showStatus("MCLK-Safe Init...", TFT_YELLOW);
  
  // 1. Reset
  es_write(0x00, 0x1F); delay(50);
  es_write(0x00, 0x00); delay(50);
  
  // 2. Clocking (Values from working v3.8.0)
  es_write(0x01, 0x3F); // Critical: MCLK divider
  es_write(0x02, 0x00); 
  es_write(0x03, 0x10); 
  es_write(0x04, 0x10);
  es_write(0x05, 0x00);
  es_write(0x06, 0x04);
  es_write(0x07, 0x00);
  es_write(0x08, 0x40); // LRCK divider = 64
  
  // 3. I2S Format
  es_write(0x09, 0x00); 
  es_write(0x0A, 0x00); 
  
  // 4. Power & Analog
  es_write(0x0D, 0x01); 
  es_write(0x0E, 0x02); 
  es_write(0x0F, 0x7F);
  es_write(0x10, 0x00);
  es_write(0x11, 0x7C);
  
  // 5. ADC (Mic) - just in case
  es_write(0x13, 0x10); 
  es_write(0x14, 0x1A); 
  
  // 6. DAC & Output Routing (КРИТИЧНО)
  es_write(0x12, 0x00); // Unmute
  es_write(0x31, 0x00); 
  es_write(0x32, 0xBF); // Vol MAX
  es_write(0x37, 0x48); // Route DAC to output (Working value!)
  es_write(0x45, 0x00); // Driver Gain (Working value!)
  
  // 7. Master Start
  es_write(0x00, 0x80); 
  delay(50);

  // Verification
  uint8_t v01 = es_read(0x01);
  Serial.printf("[DIAG] Reg 0x01 = 0x%02X (Expected 0x3F)\n", v01);
  if (v01 == 0x3F) showStatus("Codec: OK!", TFT_GREEN);
  else showStatus("Codec: MCLK ERR", TFT_RED);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  lcd.init();
  lcd.setRotation(0);
  lcd.fillScreen(TFT_BLACK);
  lcd.setTextSize(2);
  lcd.setCursor(0, 0);
  
  showStatus("MELVIN v6.1", TFT_CYAN);
  
  // 1. PA MUTE
  pinMode(PA_CTRL_PIN, OUTPUT);
  digitalWrite(PA_CTRL_PIN, LOW);
  showStatus("AMP: Muted", TFT_YELLOW);

  // 2. Start I2C
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(100000);

  // 3. Start I2S (This starts MCLK on GPIO16)
  audio.setPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
  audio.setVolume(21);
  audio.forceMono(true);
  showStatus("I2S/MCLK Start", TFT_GREEN);
  delay(200); // Даем MCLK стабилизироваться

  // 4. Init Codec while MCLK is active
  initCodec();

  // 5. PA UNMUTE
  digitalWrite(PA_CTRL_PIN, HIGH);
  showStatus("AMP: ACTIVE", TFT_GREEN);

  // SD
  SD_MMC.setPins(SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  if (SD_MMC.begin("/sdcard", true)) {
    sdCardInitialized = true;
    showStatus("SD: OK", TFT_GREEN);
    if (tts.begin()) {
      showStatus("TTS: Ready", TFT_GREEN);
      if (SD_MMC.exists("/hello.wav")) {
        showStatus("Playing...", TFT_MAGENTA);
        audio.connecttoFS(SD_MMC, "/hello.wav");
      }
    }
  } else {
    showStatus("SD: FAIL", TFT_RED);
  }
}

void loop() {
  audio.loop();
  
  static unsigned long lastBtnPressTime = 0;
  static int clickCount = 0;
  static bool lastBtnState = HIGH;
  
  bool currentBtnState = digitalRead(BOOT_BUTTON_PIN);
  
  if (lastBtnState == HIGH && currentBtnState == LOW) {
    lastBtnPressTime = millis();
    clickCount++;
    Serial.printf("[BTN] Click %d\n", clickCount);
  }
  lastBtnState = currentBtnState;

  if (clickCount > 0 && (millis() - lastBtnPressTime > 400)) {
    if (sdCardInitialized) {
      if (clickCount == 1) {
        audio.connecttoFS(SD_MMC, "/hello.wav");
      } 
      else if (clickCount == 2) {
        tts.speakRandomPhrase();
      }
      else {
        showStatus("Re-init Codec", TFT_YELLOW);
        initCodec();
      }
    }
    clickCount = 0;
  }
}

void audio_info(const char *info) { Serial.printf("[AUDIO] %s\n", info); }
void audio_eof_mp3(const char *info) { Serial.println("[AUDIO] EOF"); }
