#include <Arduino.h>
#include "Audio.h"
#include <SD_MMC.h>

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

// Кнопка BOOT
#define BOOT_BUTTON_PIN 0

// Аудио объект
Audio audio;

// Флаги состояния
bool sdCardInitialized = false;
bool audioInitialized = false;

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
  Serial.println("Кнопка:");
  Serial.printf("  BOOT: GPIO%d\n", BOOT_BUTTON_PIN);
  Serial.println("=============================================");
}

bool initSDCard() {
  Serial.println("Инициализация SD карты через SD_MMC...");
  
  if (!SD_MMC.begin("/sdcard", true)) {  // 1-bit mode
    Serial.println("Ошибка инициализации SD карты");
    return false;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("SD карта не найдена");
    return false;
  }
  
  Serial.print("Тип SD карты: ");
  switch (cardType) {
    case CARD_MMC: Serial.println("MMC"); break;
    case CARD_SD: Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC"); break;
    default: Serial.println("Неизвестно"); break;
  }
  
  uint64_t cardSize = SD_MMC.cardSize() / (1024 * 1024);
  Serial.printf("Объем SD карты: %llu MB\n", cardSize);
  
  sdCardInitialized = true;
  return true;
}

bool initAudio() {
  Serial.println("Настройка аудио I2S...");
  
  // Конфигурация I2S для ES8311
  // Используем setPinout с отдельными пинами (BCLK, LRCK, DOUT, DIN, MCLK)
  audio.setPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN, I2S_DIN_PIN, I2S_MCLK_PIN);
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
  
  File root = SD_MMC.open("/");
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
  
  if (!SD_MMC.exists("/hello.wav")) {
    Serial.println("  ОШИБКА: Файл hello.wav не найден на SD карте!");
    Serial.println("  Убедитесь, что файл находится в корне SD карты");
    return false;
  }
  
  File wavFile = SD_MMC.open("/hello.wav");
  if (!wavFile) {
    Serial.println("  ОШИБКА: Не удалось открыть файл hello.wav");
    return false;
  }
  
  Serial.printf("  Файл найден: %d байт\n", wavFile.size());
  wavFile.close();
  
  // Воспроизведение файла
  audio.connecttoFS(SD_MMC, "/hello.wav");
  Serial.println("  ✓ Воспроизведение начато");
  
  return true;
}

void setup() {
  Serial.begin(115200);
  delay(3000);
  
  Serial.println();
  Serial.println("=============================================");
  Serial.println("  SpotPear ESP32-S3-1.54 V2.0 — Arduino");
  Serial.println("  Упрощенная версия — воспроизведение WAV");
  Serial.println("=============================================");
  
  // Вывод информации о пинах
  printPinsInfo();
  
  // 1. Включение усилителя
  enableAmplifier();
  
  // 2. Инициализация SD карты
  if (!initSDCard()) {
    Serial.println("ПРОДОЛЖЕНИЕ БЕЗ SD КАРТЫ");
  } else {
    // 3. Показать содержимое SD карты
    listFilesOnSD();
  }
  
  // 4. Инициализация аудио
  if (!initAudio()) {
    Serial.println("КРИТИЧЕСКАЯ ОШИБКА: Не удалось инициализировать аудио");
    while(1) { delay(1000); }
  }
  
  // 5. Воспроизведение hello.wav
  if (sdCardInitialized) {
    if (!playHelloWav()) {
      Serial.println("Файл hello.wav не найден");
    }
  } else {
    Serial.println("SD карта не инициализирована");
  }
  
  Serial.println("=============================================");
  Serial.println("Готово! Система запущена.");
  Serial.println("Нажмите BOOT для повторного воспроизведения");
  Serial.println("=============================================");
}

void loop() {
  audio.loop();
  
  // Проверка кнопки BOOT для повторного воспроизведения
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(BOOT_BUTTON_PIN);
  
  if (lastButtonState == HIGH && buttonState == LOW) {
    // Кнопка нажата
    Serial.println("\n>>> Кнопка BOOT нажата <<<");
    
    if (sdCardInitialized && SD_MMC.exists("/hello.wav")) {
      Serial.println("Повторное воспроизведение hello.wav");
      audio.stopSong();
      delay(100);
      audio.connecttoFS(SD_MMC, "/hello.wav");
    } else {
      Serial.println("hello.wav не найден");
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