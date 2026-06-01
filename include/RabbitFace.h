#pragma once
#include <LovyanGFX.hpp>
#include "MelvinState.h"

// ============================================================
// RabbitFace — рисует морду зайца-робота на 240×240 дисплее
// ============================================================

// Цвета (RGB565)
#define COL_BG       0x0810  // тёмно-синий фон
#define COL_WHITE    0xEF7B  // светло-серый заяц
#define COL_PINK     0xFCAF  // розовый внутри ушей / щёки
#define COL_DARK     0x2104  // тёмно-серый глаза
#define COL_SHINE    0xFFFF  // белый блик
#define COL_CYAN     0x07FF  // акценты робота
#define COL_RED      0xF800
#define COL_GREEN    0x07E0
#define COL_YELLOW   0xFFE0
#define COL_ORANGE   0xFC60

static void _drawRabbitBase(lgfx::LovyanGFX& d) {
    // --- Уши ---
    d.fillRoundRect(78,  8, 28, 65, 14, COL_WHITE);   // левое
    d.fillRoundRect(134, 8, 28, 65, 14, COL_WHITE);   // правое
    d.fillRoundRect(84, 13, 16, 52, 10, COL_PINK);    // розовый внутри левого
    d.fillRoundRect(140, 13, 16, 52, 10, COL_PINK);   // розовый внутри правого

    // --- Голова ---
    d.fillCircle(120, 148, 82, COL_WHITE);

    // --- Щёчки ---
    d.fillEllipse(78,  158, 18, 11, COL_PINK);
    d.fillEllipse(162, 158, 18, 11, COL_PINK);

    // --- Нос ---
    d.fillTriangle(116, 158, 124, 158, 120, 165, COL_PINK);

    // --- Робото-акцент: ошейник ---
    d.drawRoundRect(95, 218, 50, 14, 5, COL_CYAN);
    d.fillRect(108, 221, 6, 8, COL_CYAN);
    d.fillRect(126, 221, 6, 8, COL_CYAN);
}

// ----- IDLE — спокойный, глаза иногда моргают -----
inline void drawRabbitIdle(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(COL_BG);
    _drawRabbitBase(d);

    bool blink = (tick % 100 > 95);
    if (blink) {
        // Закрытые глаза — дуги
        d.drawArc(97,  138, 14, 10, 200, 340, COL_DARK);
        d.drawArc(143, 138, 14, 10, 200, 340, COL_DARK);
    } else {
        d.fillEllipse(97,  138, 16, 13, COL_DARK);
        d.fillEllipse(143, 138, 16, 13, COL_DARK);
        d.fillCircle(103, 133, 5, COL_SHINE);
        d.fillCircle(149, 133, 5, COL_SHINE);
    }
    // Улыбка
    d.drawArc(120, 175, 14, 10, 210, 330, COL_DARK);

    // Статус
    d.setTextColor(COL_CYAN);
    d.setTextSize(1);
    d.setCursor(72, 232);
    d.print("[ МЕЛВИН v7.0 ]");
}

// ----- RECORDING — рот открыт, пульсирующий REC -----
inline void drawRabbitRecording(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(0x2000); // тёмно-красный фон
    _drawRabbitBase(d);

    // Широко открытые глаза
    d.fillEllipse(97,  138, 18, 15, COL_DARK);
    d.fillEllipse(143, 138, 18, 15, COL_DARK);
    d.fillCircle(104, 132, 5, COL_SHINE);
    d.fillCircle(150, 132, 5, COL_SHINE);

    // Открытый рот
    d.fillEllipse(120, 178, 16, 10, COL_DARK);

    // Пульсирующий кружок REC
    bool pulse = (tick % 20) < 10;
    if (pulse) d.fillCircle(205, 25, 10, COL_RED);
    else       d.drawCircle(205, 25, 10, COL_RED);
    d.setTextColor(COL_RED);
    d.setTextSize(1);
    d.setCursor(178, 40);
    d.print("REC");

    // Звуковые волны
    int wave = (tick % 10);
    d.drawArc(120, 178, 24 + wave,     22 + wave,     210, 330, COL_RED);
    d.drawArc(120, 178, 34 + wave * 2, 32 + wave * 2, 210, 330, 0xF810);

    d.setTextColor(COL_RED);
    d.setCursor(62, 232);
    d.print("ГОВОРИ — СНОВА ДЛЯ СТОП");
}

// ----- THINKING — глаза вверх, точки "..." -----
inline void drawRabbitThinking(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(0x0018); // тёмно-фиолетовый
    _drawRabbitBase(d);

    // Глаза смотрят вверх
    d.fillEllipse(97,  135, 16, 13, COL_DARK);
    d.fillEllipse(143, 135, 16, 13, COL_DARK);
    d.fillCircle(101, 130, 5, COL_SHINE);
    d.fillCircle(147, 130, 5, COL_SHINE);

    // Задумчивый рот
    d.drawLine(108, 180, 132, 180, COL_DARK);

    // Точки думаю...
    int shown = (tick / 20) % 4;
    uint16_t dotCol = COL_CYAN;
    if (shown > 0) d.fillCircle(105, 210, 6, dotCol);
    if (shown > 1) d.fillCircle(120, 210, 6, dotCol);
    if (shown > 2) d.fillCircle(135, 210, 6, dotCol);

    // Облачко мысли
    d.drawCircle(170, 80, 18, COL_CYAN);
    d.drawCircle(158, 90, 10, COL_CYAN);
    d.setTextColor(COL_CYAN);
    d.setTextSize(1);
    d.setCursor(162, 74);
    d.print("AI");

    d.setTextColor(COL_CYAN);
    d.setCursor(78, 232);
    d.print("ДУМАЮ...");
}

// ----- SPEAKING — рот анимирован -----
inline void drawRabbitSpeaking(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(0x0410); // тёмно-зелёный
    _drawRabbitBase(d);

    // Весёлые глаза
    d.fillEllipse(97,  138, 16, 13, COL_DARK);
    d.fillEllipse(143, 138, 16, 13, COL_DARK);
    d.fillCircle(103, 133, 5, COL_SHINE);
    d.fillCircle(149, 133, 5, COL_SHINE);

    // Анимированный рот (открыт/закрыт)
    int mouthH = 5 + (tick % 10 < 5 ? tick % 10 : 10 - tick % 10) * 2;
    d.fillEllipse(120, 178, 15, mouthH, COL_DARK);

    // Звуковые волны
    for (int i = 1; i <= 3; i++) {
        int rx = 22 + i * 10 + (tick % 8);
        uint16_t col = (i == 1) ? COL_GREEN : (i == 2 ? 0x07C0 : 0x0380);
        d.drawArc(120, 178, rx + 2, rx, 170, 10, col);
        d.drawArc(120, 178, rx + 2, rx, 350, 190, col);
    }

    d.setTextColor(COL_GREEN);
    d.setCursor(72, 232);
    d.print("ГОВОРЮ...");
}

// ----- CONFIG AP — настройка WiFi -----
inline void drawRabbitConfig(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(0x1820);
    _drawRabbitBase(d);

    d.fillEllipse(97,  138, 16, 13, COL_DARK);
    d.fillEllipse(143, 138, 16, 13, COL_DARK);
    d.fillCircle(103, 133, 4, COL_SHINE);
    d.fillCircle(149, 133, 4, COL_SHINE);
    d.drawArc(120, 175, 14, 10, 210, 330, COL_DARK);

    // WiFi иконка
    int a = tick % 60;
    d.drawArc(120, 90, 30, 28, 210, 330, a > 10 ? COL_YELLOW : COL_BG);
    d.drawArc(120, 90, 20, 18, 210, 330, a > 20 ? COL_YELLOW : COL_BG);
    d.drawArc(120, 90, 10,  8, 210, 330, a > 30 ? COL_YELLOW : COL_BG);
    d.fillCircle(120, 94, 4, COL_YELLOW);

    d.setTextColor(COL_YELLOW);
    d.setTextSize(1);
    d.setCursor(48, 210);
    d.print("WiFi: Melvin-Setup");
    d.setCursor(42, 222);
    d.print("IP: 192.168.4.1");
}

// ----- CONNECTING -----
inline void drawRabbitConnecting(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(COL_BG);
    _drawRabbitBase(d);

    d.fillEllipse(97,  138, 16, 13, COL_DARK);
    d.fillEllipse(143, 138, 16, 13, COL_DARK);
    d.fillCircle(103, 133, 4, COL_SHINE);
    d.fillCircle(149, 133, 4, COL_SHINE);

    // Вращающийся спиннер
    int angle = (tick * 6) % 360;
    d.drawArc(120, 90, 22, 18, angle, angle + 90, COL_CYAN);
    d.drawArc(120, 90, 22, 18, angle + 180, angle + 270, COL_CYAN);

    d.setTextColor(COL_CYAN);
    d.setCursor(68, 210);
    d.print("ПОДКЛЮЧЕНИЕ...");
}

// ----- ERROR -----
inline void drawRabbitError(lgfx::LovyanGFX& d, uint32_t tick) {
    d.fillScreen(0x3000);
    _drawRabbitBase(d);

    // X глаза
    d.drawLine(84, 126, 110, 150, COL_RED); d.drawLine(110, 126, 84, 150, COL_RED);
    d.drawLine(130, 126, 156, 150, COL_RED); d.drawLine(156, 126, 130, 150, COL_RED);

    // Грустный рот
    d.drawArc(120, 190, 14, 10, 30, 150, COL_DARK);

    d.setTextColor(COL_RED);
    d.setCursor(88, 210);
    d.print("ОШИБКА!");
}

// ----- Главная функция отрисовки -----
inline void drawFace(lgfx::LovyanGFX& d, RobotState state, uint32_t tick) {
    switch (state) {
        case STATE_IDLE:       drawRabbitIdle(d, tick);       break;
        case STATE_RECORDING:  drawRabbitRecording(d, tick);  break;
        case STATE_THINKING:   drawRabbitThinking(d, tick);   break;
        case STATE_SPEAKING:   drawRabbitSpeaking(d, tick);   break;
        case STATE_CONFIG_AP:  drawRabbitConfig(d, tick);     break;
        case STATE_CONNECTING: drawRabbitConnecting(d, tick); break;
        case STATE_ERROR:      drawRabbitError(d, tick);      break;
        default:               d.fillScreen(COL_BG);          break;
    }
}
