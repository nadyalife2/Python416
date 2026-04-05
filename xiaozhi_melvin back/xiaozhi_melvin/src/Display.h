#pragma once
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      // --- ШИНА SPI3_HOST (КРИТИЧНО ДЛЯ MUMA 1.54) ---
      // Используем SPI3 для бесконфликтной работы с другими периферийными устройствами
      cfg.spi_host = SPI3_HOST;
      cfg.spi_mode = 0;
      cfg.freq_write = 40000000;
      cfg.freq_read = 16000000;
      cfg.spi_3wire = true;
      cfg.use_lock = true;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      
      // Пины шины SPI
      cfg.pin_sclk = 4;    // SCLK (Тактовая частота)
      cfg.pin_mosi = 2;    // MOSI (Передача данных)
      cfg.pin_miso = -1;
      cfg.pin_dc   = 47;   // DC (Data/Command)
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    {
      auto cfg = _panel_instance.config();
      // Пины дисплея
      cfg.pin_cs = 5;      // CS
      cfg.pin_rst = 38;    // RST
      cfg.pin_busy = -1;
      
      // Настройки ST7789
      cfg.memory_width = 240;
      cfg.memory_height = 240;
      cfg.panel_width = 240;
      cfg.panel_height = 240;
      cfg.offset_x = 0;
      cfg.offset_y = 0; 
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true; 
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }

    {
      auto cfg = _light_instance.config();
      // Подсветка дисплея
      cfg.pin_bl = 42;     // BL
      cfg.invert = false;
      cfg.freq = 44100;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }

    setPanel(&_panel_instance);
  }
};
