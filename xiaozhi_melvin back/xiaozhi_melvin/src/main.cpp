#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <FS.h>
#include <vector>
#include <ESPAsyncWebServer.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <Wire.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "Display.h"
#include "Audio.h"

// =================================================================
// MELVIN v3.9.0 - AUDIO SUBSYSTEM REFACTOR
// =================================================================

// --- GPIO PINS (STRICT MAPPING) ---
#define BOOT_BTN      0
#define SDMMC_CMD_PIN 18
#define SDMMC_CLK_PIN 17
#define SDMMC_D0_PIN  21

#define I2C_SDA       15
#define I2C_SCL       14
#define PA_ENABLE     46

#define I2S_MCLK_PIN  GPIO_NUM_16
#define I2S_BCLK_PIN  GPIO_NUM_9
#define I2S_WS_PIN    GPIO_NUM_45
#define I2S_DIN_PIN   GPIO_NUM_10   // ES8311 -> ESP32 (Microphone)
#define I2S_DOUT_PIN  GPIO_NUM_8    // ESP32 -> ES8311 (Speaker)

// For Audio.h library (integer pins)
#define I2S_MCLK_NUM  16
#define I2S_BCLK_NUM  9
#define I2S_LRC_NUM   45
#define I2S_DOUT_NUM  8

#define ES8311_ADDR   0x18
#define SAMPLE_RATE   16000

// --- API KEYS (Removed for security, please use config.txt on SD) ---
String hfApiKey = ""; 
String orApiKey = "";

const String HF_STT_URL = "https://router.huggingface.co/models/openai/whisper-large-v3-turbo";
const String HF_TTS_URL = "https://router.huggingface.co/models/facebook/mms-tts-rus";
const String OR_URL     = "https://openrouter.ai/api/v1/chat/completions";
const String OR_MODEL   = "meta-llama/llama-3.1-8b-instruct:free";

const String SYSTEM_PROMPT =
  "You are Rick Sanchez C-137. Be rude, sarcastic, and use scientific jargon. "
  "Respond in Russian, max 2-3 sentences. "
  "ALWAYS start the response with an emotion tag: [NEUTRAL], [HAPPY], [THINKING], [ERROR_STATE].";

// --- GLOBAL OBJECTS ---
LGFX lcd;
Audio* audio = nullptr; 
AsyncWebServer server(80);

bool sdReady = false;
bool wifiConnected = false;
bool i2sRxActive = false;
static i2s_chan_handle_t rx_handle = NULL;
std::vector<String> rssFeeds;

enum MicRoute { MIC_ROUTE_MIC1, MIC_ROUTE_MIC2 };
enum MicSlot  { MIC_SLOT_LEFT,  MIC_SLOT_RIGHT };
static MicRoute g_mic_route = MIC_ROUTE_MIC1;
static MicSlot  g_mic_slot  = MIC_SLOT_LEFT;

enum EmotionState { NEUTRAL, THINKING, HAPPY, NEWS, ERROR_STATE, LISTENING, SPEAKING, EMO_WIFI_AP };
volatile EmotionState currentEmotion = NEUTRAL;
SemaphoreHandle_t lcdMutex;
TaskHandle_t animationTaskHandle;

volatile int connState = 0;
String connIP = "";

// =================================================================
// BOD DISABLE
// =================================================================
static void __attribute__((constructor(101))) disable_bod() {
  REG_CLR_BIT(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA);
}

// =================================================================
// WAV HEADER GENERATOR
// =================================================================
void writeWavHeader(uint8_t* hdr, uint32_t dataBytes) {
  uint32_t sampleRate = 16000;
  uint16_t channels = 1;
  uint16_t bitsPerSample = 16;
  uint32_t byteRate = sampleRate * channels * bitsPerSample / 8;
  uint16_t blockAlign = channels * bitsPerSample / 8;
  uint32_t chunkSize = 36 + dataBytes;
  memcpy(hdr,    "RIFF", 4); memcpy(hdr+4,  &chunkSize,    4);
  memcpy(hdr+8,  "WAVE", 4); memcpy(hdr+12, "fmt ",        4);
  uint32_t sub1 = 16;        memcpy(hdr+16, &sub1,          4);
  uint16_t pcm = 1;          memcpy(hdr+20, &pcm,           2);
  memcpy(hdr+22, &channels,      2); memcpy(hdr+24, &sampleRate,   4);
  memcpy(hdr+28, &byteRate,      4); memcpy(hdr+32, &blockAlign,   2);
  memcpy(hdr+34, &bitsPerSample, 2); memcpy(hdr+36, "data",        4);
  memcpy(hdr+40, &dataBytes,     4);
}

// =================================================================
// ES8311 I2C HELPERS
// =================================================================
static bool es_write(uint8_t reg, uint8_t val) {
  for (int i = 0; i < 3; i++) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg); Wire.write(val);
    if (Wire.endTransmission() == 0) return true;
    delay(5);
  }
  return false;
}

static uint8_t es_read(uint8_t reg) {
  Wire.beginTransmission(ES8311_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

// =================================================================
// AUDIO LIFECYCLE MANAGER
// =================================================================

static void audio_release_i2s() {
    Serial.println("[AUDIO] Releasing I2S...");
    if (audio) {
        audio->stopSong();
        delete audio;
        audio = nullptr;
    }
    if (rx_handle) {
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        rx_handle = NULL;
    }
    i2sRxActive = false;
    digitalWrite(PA_ENABLE, LOW);
    delay(50);
}

static void audio_enter_tx_mode() {
    audio_release_i2s();
    Serial.println("[AUDIO] TX MODE Start");
    
    digitalWrite(PA_ENABLE, HIGH);
    delay(60); 

    if (!audio) audio = new Audio();
    audio->setPinout(I2S_BCLK_NUM, I2S_LRC_NUM, I2S_DOUT_NUM, I2S_MCLK_NUM);
    audio->setVolume(15);
}

static bool audio_enter_rx_mode() {
    audio_release_i2s();
    Serial.println("[AUDIO] RX MODE Start");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, NULL, &rx_handle) != ESP_OK) return false;

    i2s_std_clk_config_t clk = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    clk.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    slot.slot_mask = I2S_STD_SLOT_LEFT; 

    i2s_std_gpio_config_t pins = {
      .mclk = I2S_MCLK_PIN, .bclk = I2S_BCLK_PIN, .ws = I2S_WS_PIN, .dout = I2S_DOUT_PIN, .din = I2S_DIN_PIN
    };

    i2s_std_config_t cfg = { .clk_cfg = clk, .slot_cfg = slot, .gpio_cfg = pins };
    
    if (i2s_channel_init_std_mode(rx_handle, &cfg) != ESP_OK) return false;
    if (i2s_channel_enable(rx_handle) != ESP_OK) return false;

    i2sRxActive = true;
    return true;
}

// =================================================================
// ES8311 INITIALIZATION
// =================================================================
void initES8311() {
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);
  delay(20);

  // Power Up sequence
  es_write(0x00, 0x1F); delay(20);
  es_write(0x00, 0x80); delay(20);

  // Static Register Map
  es_write(0x01, 0x3F);
  es_write(0x02, 0x00);
  es_write(0x03, 0x10);
  es_write(0x04, 0x10);
  es_write(0x05, 0x00);
  es_write(0x06, 0x04);
  es_write(0x07, 0x00);
  es_write(0x08, 0x40);
  es_write(0x09, 0x00);
  es_write(0x0A, 0x00);
  es_write(0x0D, 0x01);
  es_write(0x0E, 0x02);
  es_write(0x0F, 0x7F);
  es_write(0x10, 0x00);
  es_write(0x11, 0x7C);
  es_write(0x12, 0x00);
  es_write(0x13, 0x10);

  // Mic Route (MIC1 or MIC2)
  uint8_t mic_reg = (g_mic_route == MIC_ROUTE_MIC1) ? 0x1F : 0x3F;
  es_write(0x14, mic_reg);

  es_write(0x15, 0x40);
  es_write(0x16, 0x24);
  es_write(0x17, 0xBF); // ADC digital vol 0 dB
  es_write(0x18, 0x00); // ALC off
  es_write(0x19, 0x00);
  es_write(0x1A, 0x00);
  es_write(0x1B, 0x00);
  es_write(0x1C, 0x6A); // HPF on
  es_write(0x31, 0x60);
  es_write(0x32, 0xBF);
  es_write(0x37, 0x48);
  es_write(0x45, 0x00);

  Serial.println("[CODEC] ES8311 Ready");
}

static void mic_self_test() {
    struct Test { MicRoute r; MicSlot s; const char* n; } tests[] = {
        { MIC_ROUTE_MIC1, MIC_SLOT_LEFT,  "MIC1+LEFT"  },
        { MIC_ROUTE_MIC1, MIC_SLOT_RIGHT, "MIC1+RIGHT" },
        { MIC_ROUTE_MIC2, MIC_SLOT_LEFT,  "MIC2+LEFT"  },
        { MIC_ROUTE_MIC2, MIC_SLOT_RIGHT, "MIC2+RIGHT" }
    };
    Serial.println("\n[DIAG] Starting Mic Matrix...");
    for (auto &t : tests) {
        g_mic_route = t.r; g_mic_slot = t.s;
        initES8311(); delay(50);
        if (!audio_enter_rx_mode()) continue;

        int16_t buf[512*2]; size_t br=0; int32_t peak=0;
        long start = millis();
        while(millis()-start < 1000) {
            i2s_channel_read(rx_handle, buf, 2048, &br, 100);
            for(int i=0; i<(int)(br/4); i++) {
                int16_t s = (g_mic_slot == MIC_SLOT_LEFT) ? buf[i*2] : buf[i*2+1];
                if(abs(s)>peak) peak=abs(s);
            }
        }
        float db = (peak > 0) ? 20.0f * log10f(peak / 32767.0f) : -96.0f;
        Serial.printf("[DIAG] %s -> Peak %d (%.1f dBFS)\n", t.n, peak, db);
        audio_release_i2s();
    }
}

// =================================================================
// DISPLAY & ANIMATION
// =================================================================
void animationTask(void *pvParameters) {
  LGFX_Sprite face(&lcd);
  face.setPsram(true);
  face.createSprite(240, 140);
  while(true) {
    face.fillSprite(TFT_BLACK);
    int cx = 120, cy = 70;
    unsigned long t = millis();
    switch (currentEmotion) {
      case NEUTRAL:
        face.fillRect(cx-50,cy-20,30,10,TFT_WHITE);
        face.fillRect(cx+20,cy-20,30,10,TFT_WHITE);
        face.drawFastHLine(cx-30,cy+30,60,TFT_WHITE);
        break;
      case THINKING: {
        int h=4+(int)((sin(t/200.0)+1)*3);
        face.fillRect(cx-50,cy-10,30,h,TFT_CYAN);
        face.fillRect(cx+20,cy-10,30,h,TFT_CYAN);
        face.fillCircle(cx,cy+30,5,TFT_CYAN);
        break;
      }
      case HAPPY:
        face.drawArc(cx-35,cy-10,15,10,0,180,TFT_GREEN);
        face.drawArc(cx+35,cy-10,15,10,0,180,TFT_GREEN);
        face.drawArc(cx,cy+20,40,38,0,180,TFT_GREEN);
        break;
      case LISTENING: {
        int r=10+(int)((sin(t/100.0)+1)*8);
        face.fillCircle(cx-35,cy-10,r,TFT_MAGENTA);
        face.fillCircle(cx+35,cy-10,r,TFT_MAGENTA);
        break;
      }
      case SPEAKING: {
        face.fillRect(cx-50,cy-20,30,10,TFT_WHITE);
        face.fillRect(cx+20,cy-20,30,10,TFT_WHITE);
        int mh=5+(int)((sin(t/50.0)+1)*6);
        face.fillEllipse(cx,cy+30,30,mh,TFT_WHITE);
        break;
      }
      default: break;
    }
    xSemaphoreTake(lcdMutex, portMAX_DELAY);
    lcd.startWrite(); face.pushSprite(0,0); lcd.endWrite();
    xSemaphoreGive(lcdMutex);
    vTaskDelay(pdMS_TO_TICKS(30));
  }
}

void printTextBounded(String text, uint16_t color) {
  xSemaphoreTake(lcdMutex, portMAX_DELAY);
  lcd.fillRect(0,145,240,135,TFT_BLACK);
  lcd.setCursor(0,150); lcd.setTextColor(color, TFT_BLACK);
  lcd.setTextWrap(true, true); lcd.println(text);
  xSemaphoreGive(lcdMutex);
}

// =================================================================
// SD CONFIG
// =================================================================
bool readSDConfig() {
  File file = SD_MMC.open("/config.txt");
  if (!file) return false;
  hfApiKey = file.readStringUntil('\n'); hfApiKey.trim();
  orApiKey = file.readStringUntil('\n'); orApiKey.trim();
  while(file.available()){ String f=file.readStringUntil('\n'); f.trim(); if(f.length()>5) rssFeeds.push_back(f); }
  file.close();
  return true;
}

// =================================================================
// SPEECH & TRANSCRIPTION
// =================================================================

void speakText(const String& text) {
  if (!wifiConnected) { printTextBounded(text, TFT_CYAN); return; }
  currentEmotion = SPEAKING;
  printTextBounded(text, TFT_CYAN);
  if (animationTaskHandle) vTaskSuspend(animationTaskHandle);

  bool played = false;
  if (sdReady && hfApiKey.length() > 5) {
    WiFiClientSecure client; client.setInsecure();
    HTTPClient http; http.begin(client, HF_TTS_URL);
    http.addHeader("Authorization", "Bearer " + hfApiKey);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(25000);
    String escaped = text; escaped.replace("\"", "\\\"");
    int code = http.POST("{\"inputs\":\"" + escaped + "\"}");
    if (code == 200) {
      File f = SD_MMC.open("/tts_out.wav", FILE_WRITE);
      if (f) {
        http.writeToStream(&f); f.close();
        http.end();
        audio_enter_tx_mode();
        audio->connecttoFS(SD_MMC, "/tts_out.wav");
        while(audio->isRunning()) { audio->loop(); delay(1); }
        audio->stopSong();
        played = true;
      }
    }
    http.end();
  }

  if (!played) {
    audio_enter_tx_mode();
    audio->connecttospeech(text.c_str(), "ru");
    while(audio->isRunning()) { audio->loop(); delay(1); }
    audio->stopSong();
  }

  if (animationTaskHandle) vTaskResume(animationTaskHandle);
  currentEmotion = NEUTRAL;
}

String recordAndTranscribe() {
  audio_release_i2s();
  if (!wifiConnected) return "";
  currentEmotion = LISTENING; printTextBounded("LISTENING...", TFT_MAGENTA);
  if (!audio_enter_rx_mode()) return "";

  const int MAX_S = 8;
  const int PCM_SIZE = MAX_S * 16000 * 2;
  uint8_t* wav = (uint8_t*)ps_malloc(PCM_SIZE + 44);
  if(!wav) { audio_release_i2s(); return ""; }

  int16_t dma[512*2]; size_t br=0; int p=44;
  long start = millis(), lastVoice = millis();
  bool voiced = false;

  while(millis()-start < MAX_S*1000) {
    if(p + 2048 > PCM_SIZE + 44) break;
    i2s_channel_read(rx_handle, dma, 2048, &br, 100);
    int samples = br/4; int32_t peak=0;
    for(int i=0; i<samples; i++) {
        int16_t s = (g_mic_slot == MIC_SLOT_LEFT) ? dma[i*2] : dma[i*2+1];
        ((int16_t*)(wav+p))[i] = s;
        if(abs(s)>peak) peak=abs(s);
    }
    p += samples*2;
    float db = (peak > 0) ? 20.0f * log10f(peak / 32767.0f) : -96.0f;
    if (db > (voiced ? -37.0f : -30.0f)) { lastVoice = millis(); voiced = true; }
    if (voiced && (millis()-lastVoice > 800)) break;
    if (!voiced && (millis()-start > 3000)) break;
  }

  int pcm_len = p - 44;
  audio_release_i2s();
  delay(50);

  audio_enter_tx_mode();
  delay(50);

  if (pcm_len < 3200) { free(wav); return ""; }
  writeWavHeader(wav, pcm_len);

  String transcription = "";
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.begin(client, HF_STT_URL);
  http.addHeader("Authorization", "Bearer " + hfApiKey);
  http.addHeader("Content-Type", "audio/wav");
  http.setTimeout(30000);
  if (http.POST(wav, pcm_len + 44) == 200) {
      JsonDocument doc; deserializeJson(doc, http.getString());
      transcription = doc["text"].as<String>();
  }
  http.end(); free(wav);
  return transcription;
}

// =================================================================
// LLM & RICK LOGIC
// =================================================================
String askRick(const String& userText) {
  if (animationTaskHandle) vTaskSuspend(animationTaskHandle);
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http; http.begin(client, OR_URL);
  http.addHeader("Authorization","Bearer "+orApiKey);
  http.addHeader("Content-Type","application/json");
  JsonDocument doc; doc["model"]=OR_MODEL;
  JsonArray msgs=doc["messages"].to<JsonArray>();
  JsonObject s=msgs.add<JsonObject>(); s["role"]="system"; s["content"]=SYSTEM_PROMPT;
  JsonObject u=msgs.add<JsonObject>(); u["role"]="user"; u["content"]=userText;
  String body; serializeJson(doc,body);
  int code=http.POST(body);
  String res="";
  if(code==200){
      JsonDocument r; deserializeJson(r, http.getString());
      res=r["choices"][0]["message"]["content"].as<String>();
  }
  http.end(); if (animationTaskHandle) vTaskResume(animationTaskHandle);
  return res;
}

// =================================================================
// WIFI & WEB
// =================================================================
void setupWiFi() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP("Melvin-Setup","melvin123");
  // Basic auto-connect logic simplified
  if(sdReady) {
    File f = SD_MMC.open("/networks.txt");
    if(f) {
        String line = f.readStringUntil('\n'); line.trim();
        int sep = line.indexOf(':');
        if(sep > 0) {
            WiFi.begin(line.substring(0, sep).c_str(), line.substring(sep+1).c_str());
            int tries=0; while(WiFi.status()!=WL_CONNECTED && tries++<20) delay(500);
            if(WiFi.status()==WL_CONNECTED) { wifiConnected=true; Serial.println(WiFi.localIP()); }
        }
        f.close();
    }
  }
}

// =================================================================
// CORE SETUP & LOOP
// =================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- MELVIN v3.9.0 ---");

  lcdMutex = xSemaphoreCreateMutex();
  lcd.init(); lcd.fillScreen(TFT_BLACK);
  
  pinMode(BOOT_BTN, INPUT_PULLUP);
  pinMode(PA_ENABLE, OUTPUT);
  digitalWrite(PA_ENABLE, LOW);

  SD_MMC.setPins(SDMMC_CLK_PIN, SDMMC_CMD_PIN, SDMMC_D0_PIN);
  if (SD_MMC.begin("/sdcard", true, true, SDMMC_FREQ_DEFAULT)) { sdReady = true; readSDConfig(); }

  // Start MCLK before Codec Init
  audio_enter_tx_mode();
  initES8311();

  // Run Mic Diagnostic Matrix
  mic_self_test();

  // Return to stable TX mode
  audio_enter_tx_mode();

  xTaskCreatePinnedToCore(animationTask,"anim",8192,NULL,1,&animationTaskHandle,0);
  setupWiFi();
  
  Serial.println("[SYSTEM] Ready.");
  speakText("Rick is back.");
}

void loop() {
  if (audio) audio->loop();

  static bool btnPrev = HIGH;
  static unsigned long pressStart = 0;
  static bool longFired = false;
  bool btnNow = digitalRead(BOOT_BTN);

  if (btnPrev == HIGH && btnNow == LOW) { pressStart = millis(); longFired = false; }
  if (btnNow == LOW && !longFired && millis() - pressStart > 800) {
    longFired = true;
    String txt = recordAndTranscribe();
    if(txt.length()>1) speakText(askRick(txt));
  }
  btnPrev = btnNow;
  delay(10);
}
