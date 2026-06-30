#pragma once

enum RobotState {
    STATE_BOOT,
    STATE_CONFIG_AP,   // нет WiFi — точка доступа
    STATE_CONNECTING,  // подключение к WiFi
    STATE_IDLE,        // ждём кнопку
    STATE_WAKE_CHECK,  // проверка кодового слова
    STATE_RECORDING,   // запись голоса
    STATE_THINKING,    // запрос к AI
    STATE_SPEAKING,    // воспроизведение ответа
    STATE_ERROR
};

inline const char* stateName(RobotState s) {
    switch(s) {
        case STATE_BOOT:       return "BOOT";
        case STATE_CONFIG_AP:  return "CONFIG";
        case STATE_CONNECTING: return "WiFi...";
        case STATE_IDLE:       return "IDLE";
        case STATE_WAKE_CHECK: return "WAKE";
        case STATE_RECORDING:  return "REC";
        case STATE_THINKING:   return "THINK";
        case STATE_SPEAKING:   return "SPEAK";
        case STATE_ERROR:      return "ERROR";
        default:               return "???";
    }
}
