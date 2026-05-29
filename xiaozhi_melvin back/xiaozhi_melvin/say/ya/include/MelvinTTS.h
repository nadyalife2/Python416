#ifndef MELVIN_TTS_H
#define MELVIN_TTS_H

#include <Arduino.h>

// Временная заглушка, пока не установится библиотека TTS
class MelvinTTS {
private:
    bool initialized;
    uint8_t volume;
    
    // Тестовые фразы для Мелвина
    static const char* TEST_PHRASES[5];
    static const int NUM_PHRASES;
    
public:
    MelvinTTS();
    
    // Инициализация TTS системы
    bool begin();
    
    // Синтез и воспроизведение речи
    bool speak(const char* text);
    bool speak(String text);
    
    // Воспроизведение тестовой фразы по индексу
    bool speakTestPhrase(int index);
    
    // Воспроизведение случайной тестовой фразы
    bool speakRandomPhrase();
    
    // Управление громкостью (0-21)
    void setVolume(uint8_t vol);
    uint8_t getVolume() const;
    
    // Проверка инициализации
    bool isInitialized() const;
    
    // Остановка воспроизведения
    void stop();
    
    // Проверка, играет ли сейчас речь
    bool isSpeaking() const;
    
    // Статическая функция для получения количества фраз
    static int getNumPhrases();
};

#endif // MELVIN_TTS_H