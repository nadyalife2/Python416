#include "MelvinTTS.h"
#include <SD_MMC.h>
#include "Audio.h"

// Внешний аудио объект из main.cpp
extern Audio audio;

// Глобальный флаг из main.cpp, чтобы не обращаться к отключенной карте
extern bool sdCardInitialized;

// Тестовые фразы для Мелвина
const char* MelvinTTS::TEST_PHRASES[5] = {
    "Привет, я Мелвин",
    "Готов к работе", 
    "Батарея заряжена",
    "Подключение установлено",
    "Ошибка, проверьте соединение"
};

const int MelvinTTS::NUM_PHRASES = 5;

MelvinTTS::MelvinTTS() : initialized(false), volume(12) {
    // Конструктор по умолчанию
}

bool MelvinTTS::begin() {
    Serial.println("[TTS] Инициализация системы синтеза речи...");
    
    // Проверяем, доступна ли SD карта для кэширования
    if (SD_MMC.cardSize() > 0) {
        Serial.println("[TTS] SD карта доступна для кэширования речи");
    } else {
        Serial.println("[TTS] Внимание: SD карта не доступна");
    }
    
    initialized = true;
    Serial.println("[TTS] Система TTS инициализирована (режим заглушки)");
    Serial.println("[TTS] Примечание: Реальная TTS библиотека будет подключена позже");
    
    return true;
}

bool MelvinTTS::speak(const char* text) {
    if (!initialized) {
        Serial.println("[TTS] Ошибка: TTS не инициализирован");
        return false;
    }
    
    Serial.printf("[TTS] Синтез речи: \"%s\"\n", text);
    
    if (!sdCardInitialized) {
        Serial.println("[TTS] Ошибка: SD карта не инициализирована! Пропускаем аудио.");
        return false;
    }

    // Определяем имя файла на основе текста фразы
    String wavName;
    if (strcmp(text, TEST_PHRASES[0]) == 0) wavName = "привет_я_мелвин.wav";
    else if (strcmp(text, TEST_PHRASES[1]) == 0) wavName = "готов_к_работе.wav";
    else if (strcmp(text, TEST_PHRASES[2]) == 0) wavName = "батарея_заряжена.wav";
    else if (strcmp(text, TEST_PHRASES[3]) == 0) wavName = "подключение_установлено.wav";
    else if (strcmp(text, TEST_PHRASES[4]) == 0) wavName = "ошибка_проверьте_соединение.wav";
    else {
        wavName = text;
        wavName.replace(" ", "_");
        wavName.replace(",", "");
        wavName.replace(".", "");
        wavName += ".wav";
    }

    // Ищем файл: сначала в корне, потом в /tts/
    String pathRoot = "/" + wavName;
    String pathTts  = "/tts/" + wavName;

    Serial.printf("[TTS] Поиск: %s ... ", pathRoot.c_str());
    if (SD_MMC.exists(pathRoot.c_str())) {
        Serial.println("найден!");
        audio.connecttoFS(SD_MMC, pathRoot.c_str());
        return true;
    }
    Serial.println("нет");

    Serial.printf("[TTS] Поиск: %s ... ", pathTts.c_str());
    if (SD_MMC.exists(pathTts.c_str())) {
        Serial.println("найден!");
        audio.connecttoFS(SD_MMC, pathTts.c_str());
        return true;
    }
    Serial.println("нет");

    // Запасной вариант — hello.wav
    Serial.println("[TTS] Файл не найден, пробуем /hello.wav ...");
    if (SD_MMC.exists("/hello.wav")) {
        audio.connecttoFS(SD_MMC, "/hello.wav");
        Serial.println("[TTS] Воспроизводим /hello.wav");
        return true;
    }

    Serial.println("[TTS] Ошибка: ни один файл не найден на SD карте!");
    return false;
}

bool MelvinTTS::speak(String text) {
    return speak(text.c_str());
}

bool MelvinTTS::speakTestPhrase(int index) {
    if (index < 0 || index >= NUM_PHRASES) {
        Serial.printf("[TTS] Ошибка: неверный индекс фразы %d (допустимо 0-%d)\n", 
                     index, NUM_PHRASES - 1);
        return false;
    }
    
    Serial.printf("[TTS] Воспроизведение тестовой фразы %d\n", index);
    return speak(TEST_PHRASES[index]);
}

bool MelvinTTS::speakRandomPhrase() {
    int index = random(0, NUM_PHRASES);
    Serial.printf("[TTS] Случайная фраза: индекс %d\n", index);
    return speakTestPhrase(index);
}

void MelvinTTS::setVolume(uint8_t vol) {
    if (vol > 21) vol = 21;
    volume = vol;
    
    // Устанавливаем громкость в аудио системе
    audio.setVolume(vol);
    Serial.printf("[TTS] Громкость установлена: %d/21\n", vol);
}

uint8_t MelvinTTS::getVolume() const {
    return volume;
}

bool MelvinTTS::isInitialized() const {
    return initialized;
}

void MelvinTTS::stop() {
    Serial.println("[TTS] Остановка воспроизведения");
    audio.stopSong();
}

bool MelvinTTS::isSpeaking() const {
    return audio.isRunning();
}

int MelvinTTS::getNumPhrases() {
    return NUM_PHRASES;
}