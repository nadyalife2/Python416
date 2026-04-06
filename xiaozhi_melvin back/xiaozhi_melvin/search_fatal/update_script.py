import re

with open('src/main.cpp', 'r') as f:
    content = f.read()

# Update I2S configuration in audio_enter_rx_mode
# Old:
#     i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
#     slot.slot_mask = I2S_STD_SLOT_BOTH; 
# New:
#     i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
#     slot.slot_mask = I2S_STD_SLOT_LEFT;

content = re.sub(
    r'i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG\(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO\);\s*slot\.slot_mask = I2S_STD_SLOT_BOTH;',
    r'i2s_std_slot_config_t slot = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);\n    slot.slot_mask = I2S_STD_SLOT_LEFT;',
    content
)

# Replace the initES8311 register sequence with the correct one from test_mic_speaker.ino
# Old sequence:
#   es_write(0x00, 0x1F); delay(20);
#   es_write(0x00, 0x80); delay(20);
#   ...
#   es_write(0x45, 0x00);
# New sequence:
#   es_write(0x01, 0x1F); delay(20);
#   es_write(0x01, 0x00); delay(20);
#   es_write(0x02, 0x00);
#   ...

init_block_pattern = r'void initES8311\(\) \{\s*Serial\.println\("\[BOOT\] Codec: Setting I2C timeout\.\.\."\);\s*Wire\.setTimeOut\(100\);\s*delay\(20\);\s*Serial\.println\("\[BOOT\] Codec: Power Up sequence\.\.\."\);.*?Serial\.println\("\[CODEC\] ES8311 Ready"\);\s*\}'

new_init_block = r'''void initES8311() {
  Serial.println("[BOOT] Codec: Setting I2C timeout...");
  Wire.setTimeOut(100); 
  delay(20);

  Serial.println("[BOOT] Codec: Power Up sequence...");
  // Сброс — точно как в рабочей прошивке MELVIN
  es_write(0x01, 0x1F); delay(20);
  es_write(0x01, 0x00); delay(20);  // <-- было 0x80, теперь 0x00!

  // Тактирование
  es_write(0x02, 0x00);
  es_write(0x03, 0x10);
  delay(10);

  // AIF: I2S, 16 бит
  es_write(0x0B, 0x00);
  es_write(0x0C, 0x00);

  // Power / System — из рабочего кода
  es_write(0x10, 0x00);
  es_write(0x11, 0xFC);
  delay(10);

  es_write(0x00, 0x80);  // chip enable
  delay(10);

  es_write(0x0D, 0x01);  // DAC timing
  es_write(0x0E, 0x02);  // ADC timing
  delay(10);

  // MIC PGA +18dB (0x28)
  es_write(0x12, 0x28);
  es_write(0x13, 0x06);  // ADC volume (усиление)

  // ADC включить
  es_write(0x16, 0x11);
  es_write(0x17, 0x11);
  delay(10);

  // DAC включить
  es_write(0x14, 0x1A);
  es_write(0x15, 0x1A);
  delay(10);

  Serial.println("[CODEC] ES8311 Ready");
}'''

content = re.sub(init_block_pattern, new_init_block, content, flags=re.DOTALL)

with open('src/main.cpp', 'w') as f:
    f.write(content)

