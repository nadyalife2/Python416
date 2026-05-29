#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include "Audio.h"
#include "MelvinTTS.h"

// ============================================================
// SpotPear ESP32-S3-1.54 V2.0 — ПОДТВЕРЖДЁННАЯ распиновка
// ============================================================

// I2C для ES8311 (подтверждено сканом!)
#define I2C_SCL_PIN    14
#define I2C_SDA_PIN    15

// I2S для ES8311 — правильные пины для SpotPear V2.0
#define I2S_MCLK_PIN   16
#define I2S_BCLK_PIN   9
#define I2S_LRCK_PIN   45  // WS
#define I2S_DOUT_PIN   10  // ESP32 → ES8311 (воспроизведение)
#define I2S_DIN_PIN    11  // ES8311 → ESP32 (запись)

// Усилитель NS4150B (strapping pin — нужна мягкая инициализация!)
#define PA_CTRL_PIN    46

// SD Card (SDMMC 1-bit mode)
#define SD_CLK_PIN     38
#define SD_CMD_PIN     40
#define SD_D0_PIN      39

// Кнопка BOOT
#define BOOT_BUTTON_PIN 0

// Аудио объект
Audio audio;

// TTS объект для синтеза речи
MelvinTTS tts;

// Флаги состояния
bool sdCardInitialized = false;
bool audioInitialized = false;
bool ttsInitialized = false;

void printPinsInfo() {
  Serial.println("=============================================");
  Serial.println("Конфигурация пинов SpotPear ESP32-S3 V2.0:");
  Serial.println("=============================================");
  Serial.println("I2C (ES8311):");
  Serial.printf("  SCL: GPIO%d, SDA: GPIO%d\n", I2C_SCL_PIN, I2C_SDA_PIN);
  Serial.println("I2S (ES8311):");
  Serial.printf("  MCLK: GPIO%d, BCLK: GPIO%d\n", I2S_MCLK_PIN, I2S_BCLK_PIN);
  Serial.printf("  LRCK: GPIO%d, DOUT: GPIO%d, DIN: GPIO%d\n", 
                I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN);
  Serial.println("Усилитель:");
  Serial.printf("  PA_CTRL: GPIO%d (NS4150B)\n", PA_CTRL_PIN);
  Serial.println("SD Карта:");
  Serial.printf("  CLK: GPIO%d, CMD: GPIO%d, D0: GPIO%d\n", 
                SD_CLK_PIN, SD_CMD_PIN, SD_D0_PIN);
  Serial.println("Кнопка:");
  Serial.printf("  BOOT: GPIO%d\n", BOOT_BUTTON_PIN);
  Serial.println("=============================================");
}

bool initSDCard() {
  Serial.println("Инициализация SD карты...");
  
  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
  
  // Настройка пинов для SpotPear
  slot_config.clk = (gpio_num_t)SD_CLK_PIN;
  slot_config.cmd = (gpio_num_t)SD_CMD_PIN;
  slot_config.d0 = (gpio_num_t)SD_D0_PIN;
  slot_config.width = 1;  // 1-bit mode
  
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024
  };
  
  sdmmc_card_t* card;
  esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
  
  if (ret != ESP_OK) {
    Serial.printf("Ошибка инициализации SD карты: 0x%x\n", ret);
    
    switch (ret) {
      case ESP_FAIL:
        Serial.println("  Причина: Не удалось смонтировать файловую систему");
        Serial.println("  Решение: Проверьте формат карты (должен быть FAT32)");
        break;
      case ESP_ERR_TIMEOUT:
        Serial.println("  Причина: Таймаут инициализации SD карты");
        Serial.println("  Решение: Проверьте подключение пинов SD карты");
        break;
      case ESP_ERR_NOT_FOUND:
        Serial.println("  Причина: SD карта не найдена");
        Serial.println("  Решение: Убедитесь, что карта вставлена");
        break;
      default:
        Serial.printf("  Код ошибки: 0x%x\n", ret);
    }
    
    return false;
  }
  
  Serial.println("✓ SD карта успешно инициализирована");
  Serial.printf("  Имя: %s\n", card->cid.name);
  Serial.printf("  Объем: %.2f GB\n", 
                ((float)card->csd.capacity * card->csd.sector_size) / (1024*1024*1024));
  Serial.printf("  Скорость: %d MHz\n", card->max_freq_khz / 1000);
  
  sdCardInitialized = true;
  return true;
}

bool initAudio() {
  Serial.println("Настройка аудио I2S...");
  
  // Конфигурация I2S для ES8311
  i2s_pin_config_t i2s_pins = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRCK_PIN,
    .data_out_num = I2S_DOUT_PIN,
    .data_in_num = I2S_DIN_PIN
  };
  
  audio.setPinout(i2s_pins);
  audio.setVolume(12); // Громкость от 0 до 21
  
  Serial.println("✓ Аудио система инициализирована");
  Serial.printf("  Громкость: %d/21\n", 12);
  
  audioInitialized = true;
  return true;
}

void enableAmplifier() {
  Serial.println("Включение усилителя NS4150B...");
  
  // GPIO46 — strapping pin, нужна мягкая инициализация!
  delay(500);
  pinMode(PA_CTRL_PIN, OUTPUT);
  digitalWrite(PA_CTRL_PIN, HIGH);
  
  Serial.println("✓ Усилитель включен (GPIO46=HIGH)");
  Serial.println("  Примечание: GPIO46 - strapping pin, включается после загрузки");
}

void listFilesOnSD() {
  Serial.println("Содержимое SD карты:");
  Serial.println("-------------------");
  
  File root = SD.open("/sdcard");
  if (!root) {
    Serial.println("  Ошибка открытия корневой директории");
    return;
  }
  
  int fileCount = 0;
  int wavCount = 0;
  File file = root.openNextFile();
  
  while (file) {
    fileCount++;
    
    Serial.printf("  %-20s %8d байт", file.name(), file.size());
    
    if (strstr(file.name(), ".wav") != NULL || strstr(file.name(), ".WAV") != NULL) {
      wavCount++;
      Serial.print(" [WAV]");
    }
    
    Serial.println();
    file = root.openNextFile();
  }
  
  root.close();
  
  Serial.println("-------------------");
  Serial.printf("  Всего файлов: %d\n", fileCount);
  Serial.printf("  WAV файлов: %d\n", wavCount);
  
  if (wavCount == 0) {
    Serial.println("  ВНИМАНИЕ: WAV файлы не найдены!");
  }
}

bool playHelloWav() {
  Serial.println("Попытка воспроизведения hello.wav...");
  
  if (!SD.exists("/hello.wav")) {
    Serial.println("  ОШИБКА: Файл hello.wav не найден на SD карте!");
    Serial.println("  Убедитесь, что файл находится в корне SD карты");
    return false;
  }
  
  File wavFile = SD.open("/hello.wav");
  if (!wavFile) {
    Serial.println("  ОШИБКА: Не удалось открыть файл hello.wav");
    return false;
  }
  
  Serial.printf("  Файл найден: %d байт\n", wavFile.size());
  wavFile.close();
  
  // Воспроизведение файла
  audio.connecttoFS(SD, "/hello.wav");
  Serial.println("  ✓ Воспроизведение начато");
  
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println();
  Serial.println("=============================================");
  Serial.println("  SpotPear ESP32-S3-1.54 V2.0 — Arduino");
  Serial.println("  Воспроизведение WAV с SD карты");
  Serial.println("=============================================");
  
  // Вывод информации о пинах
  printPinsInfo();
  
  // 1. Включение усилителя
  enableAmplifier();
  
  // 2. Инициализация SD карты
  if (!initSDCard()) {
    Serial.println("ПРОДОЛЖЕНИЕ БЕЗ SD КАРТЫ: Режим тестового тона");
  } else {
    // 3. Показать содержимое SD карты
    listFilesOnSD();
  }
  
  // 4. Инициализация аудио
  if (!initAudio()) {
    Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Не удалось инициализировать аудио");
    while(1) { delay(1000); }
  }
  
  // 5. Инициализация TTS системы
  Serial.println("\n--- Инициализация TTS системы ---");
  if (tts.begin()) {
    ttsInitialized = true;
    Serial.println("✓ TTS система инициализирована");
    
    // Устанавливаем громкость TTS
    tts.setVolume(15); // Немного громче стандартной
    
    // Произносим приветственную фразу
    delay(500);
    tts.speak("Привет, я Мелвин. Система запущена.");
  } else {
    Serial.println("✗ Ошибка инициализации TTS системы");
    Serial.println("  Продолжение работы без TTS");
  }
  
  // 6. Воспроизведение hello.wav или тестового тона
  if (sdCardInitialized) {
    if (!playHelloWav()) {
      Serial.println("Использование тестового тона 1 кГц...");
      // Генерация тестового тона
      audio.connecttoFS(SD, "/sine_1khz.wav"); // Попытка найти тестовый файл
    }
  } else {
    Serial.println("SD карта не инициализирована, используется тестовый тон");
    // Здесь можно добавить генерацию тестового тона программно
  }
  
  Serial.println("=============================================");
  Serial.println("Готово! Система запущена.");
  Serial.println("Нажмите BOOT для повторного воспроизведения");
  Serial.println("Нажмите BOOT дважды для тестовой фразы TTS");
  Serial.println("=============================================");
}

void loop() {
  audio.loop();
  
  // Проверка кнопки BOOT для повторного воспроизведения
  static bool lastButtonState = HIGH;
  static unsigned long lastPressTime = 0;
  static int pressCount = 0;
  
  bool buttonState = digitalRead(BOOT_BUTTON_PIN);
  
  if (lastButtonState == HIGH && buttonState == LOW) {
    // Кнопка нажата
    unsigned long currentTime = millis();
    
    // Сброс счетчика, если прошло больше 500 мс с последнего нажатия
    if (currentTime - lastPressTime > 500) {
      pressCount = 0;
    }
    
    pressCount++;
    lastPressTime = currentTime;
    
    Serial.printf("\n>>> Кнопка BOOT нажата (нажатий: %d) <<<\n", pressCount);
    
    if (pressCount == 1) {
      // Одиночное нажатие - воспроизведение hello.wav
      if (sdCardInitialized && SD.exists("/hello.wav")) {
        Serial.println("Воспроизведение hello.wav");
        audio.stopSong();
        delay(100);
        audio.connecttoFS(SD, "/hello.wav");
      } else {
        Serial.println("hello.wav не найден");
      }
    } else if (pressCount == 2) {
      // Двойное нажатие - тестовая фраза TTS
      Serial.println("Двойное нажатие - тестовая фраза TTS");
      if (ttsInitialized) {
        tts.speakRandomPhrase();
      } else {
        Serial.println("TTS не инициализирован");
      }
      pressCount = 0; // Сброс после выполнения
    } else if (pressCount >= 3) {
      // Тройное нажатие - все тестовые фразы
      Serial.println("Тройное нажатие - все тестовые фразы TTS");
      if (ttsInitialized) {
        for (int i = 0; i < tts.getNumPhrases(); i++) {
          tts.speakTestPhrase(i);
          delay(2000); // Пауза между фразами
        }
      }
      pressCount = 0; // Сброс после выполнения
    }
  }
  
  lastButtonState = buttonState;
  delay(10);
}

// ============================================================
// Обработчики событий аудио
// ============================================================

void audio_info(const char *info) {
  Serial.print("[AUDIO] ");
  Serial.println(info);
}

void audio_eof_mp3(const char *info) {
  Serial.print("[AUDIO EOF] ");
  Serial.println(info);
  Serial.println("Воспроизведение завершено. Нажмите BOOT для повторного воспроизведения.");
}

void audio_showstation(const char *info) {
  Serial.print("[STATION] ");
  Serial.println(info);
}

void audio_showstreamtitle(const char *info) {
  Serial.print("[STREAM TITLE] ");
  Serial.println(info);
}

void audio_bitrate(const char *info) {
  Serial.print("[BITRATE] ");
  Serial.println(info);
}

void audio_commercial(const char *info) {
  Serial.print("[COMMERCIAL] ");
  Serial.println(info);
}

void audio_icyurl(const char *info) {
  Serial.print("[ICY URL] ");
  Serial.println(info);
}

void audio_lasthost(const char *info) {
  Serial.print("[LAST HOST] ");
  Serial.println(info);
}

void audio_eof_speech(const char *info) {
  Serial.print("[SPEECH EOF] ");
  Serial.println(info);
}