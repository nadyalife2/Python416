#!/usr/bin/env python3
"""
Скрипт для создания тестового WAV файла hello.wav
Создает простой синусоидальный тон 1 кГц
"""

import wave
import struct
import math

def create_test_wav(filename="hello.wav", duration=3.0, sample_rate=16000, frequency=1000.0):
    """
    Создает WAV файл с синусоидальным тоном
    
    Args:
        filename: Имя выходного файла
        duration: Длительность в секундах
        sample_rate: Частота дискретизации (Гц)
        frequency: Частота тона (Гц)
    """
    
    # Количество сэмплов
    num_samples = int(duration * sample_rate)
    
    # Амплитуда (максимальное значение для 16-битного аудио)
    amplitude = 32767 * 0.5  # 50% от максимума
    
    # Создаем WAV файл
    with wave.open(filename, 'w') as wav_file:
        # Настройки: 1 канал (моно), 16 бит, sample_rate Гц
        wav_file.setnchannels(1)  # Моно
        wav_file.setsampwidth(2)  # 2 байта = 16 бит
        wav_file.setframerate(sample_rate)
        
        # Генерация аудиоданных
        for i in range(num_samples):
            # Синусоидальный сигнал
            value = amplitude * math.sin(2 * math.pi * frequency * i / sample_rate)
            
            # Преобразование в 16-битное целое
            packed_value = struct.pack('<h', int(value))
            
            # Запись в файл
            wav_file.writeframes(packed_value)
    
    print(f"Создан файл: {filename}")
    print(f"Параметры: {duration} сек, {sample_rate} Гц, {frequency} Гц тон")
    print(f"Размер: {num_samples} сэмплов, {num_samples * 2} байт данных")

def main():
    print("Создание тестового WAV файла для SpotPear ESP32-S3")
    print("=" * 50)
    
    try:
        create_test_wav()
        print("\nИнструкция:")
        print("1. Скопируйте созданный файл hello.wav в корень SD карты")
        print("2. Вставьте SD карту в плату SpotPear ESP32-S3")
        print("3. Загрузите прошивку и наслаждайтесь звуком!")
        
    except Exception as e:
        print(f"Ошибка: {e}")
        print("\nАльтернативные варианты:")
        print("1. Используйте Audacity для создания WAV файла:")
        print("   - Формат: WAV (Microsoft) PCM")
        print("   - Частота: 16000 Гц или 44100 Гц")
        print("   - Битность: 16 бит")
        print("   - Каналы: Моно")
        print("2. Запишите голосовое приветствие")
        print("3. Используйте любой WAV файл с правильным форматом")

if __name__ == "__main__":
    main()