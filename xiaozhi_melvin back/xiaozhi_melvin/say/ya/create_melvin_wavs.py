#!/usr/bin/env python3
"""
Скрипт для создания тестовых WAV файлов для робота Мелвина
Создает несколько WAV файлов с разными тестовыми сигналами
"""

import wave
import struct
import math
import os

def create_sine_wav(filename, duration=2.0, sample_rate=16000, frequency=1000.0):
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
    amplitude = 32767 * 0.3  # 30% от максимума
    
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
    
    print(f"  Создан: {filename} ({duration} сек, {frequency} Гц)")

def create_chirp_wav(filename, duration=3.0, sample_rate=16000, start_freq=200.0, end_freq=2000.0):
    """
    Создает WAV файл с чирп-сигналом (изменяющаяся частота)
    """
    num_samples = int(duration * sample_rate)
    amplitude = 32767 * 0.25
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        
        for i in range(num_samples):
            # Линейно изменяющаяся частота
            t = i / sample_rate
            freq = start_freq + (end_freq - start_freq) * (t / duration)
            
            value = amplitude * math.sin(2 * math.pi * freq * t)
            packed_value = struct.pack('<h', int(value))
            wav_file.writeframes(packed_value)
    
    print(f"  Создан: {filename} (чирп {start_freq}-{end_freq} Гц)")

def create_beep_sequence(filename, sample_rate=16000):
    """
    Создает последовательность бипов (как звуковой сигнал)
    """
    amplitude = 32767 * 0.4
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        
        # 3 коротких бипа
        for beep in range(3):
            # Бип длительностью 0.1 сек
            for i in range(int(0.1 * sample_rate)):
                value = amplitude * math.sin(2 * math.pi * 800 * i / sample_rate)
                packed_value = struct.pack('<h', int(value))
                wav_file.writeframes(packed_value)
            
            # Пауза 0.1 сек
            for i in range(int(0.1 * sample_rate)):
                packed_value = struct.pack('<h', 0)
                wav_file.writeframes(packed_value)
    
    print(f"  Создан: {filename} (3 бипа)")

def create_voice_like_wav(filename, duration=2.5, sample_rate=16000):
    """
    Создает WAV файл с голосоподобным сигналом (формантный синтез)
    """
    num_samples = int(duration * sample_rate)
    amplitude = 32767 * 0.35
    
    with wave.open(filename, 'w') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        
        for i in range(num_samples):
            t = i / sample_rate
            
            # Имитация голоса: основная частота + форманты
            f0 = 120  # Основная частота (базовый тон)
            
            # Форманты (резонансные частоты)
            f1 = 500 * (1 + 0.2 * math.sin(2 * math.pi * 2 * t))  # Меняющаяся форманта
            f2 = 1500
            f3 = 2500
            
            # Сумма синусоид с разными амплитудами
            value = amplitude * (
                0.6 * math.sin(2 * math.pi * f0 * t) +
                0.3 * math.sin(2 * math.pi * f1 * t) +
                0.2 * math.sin(2 * math.pi * f2 * t) +
                0.1 * math.sin(2 * math.pi * f3 * t)
            )
            
            # Огибающая для естественного звучания
            envelope = 1.0
            if t < 0.1:
                envelope = t / 0.1  # Атака
            elif t > duration - 0.1:
                envelope = (duration - t) / 0.1  # Затухание
            
            value *= envelope
            
            packed_value = struct.pack('<h', int(value))
            wav_file.writeframes(packed_value)
    
    print(f"  Создан: {filename} (голосоподобный сигнал)")

def create_tts_test_files():
    """
    Создает тестовые WAV файлы для TTS фраз Мелвина
    """
    phrases = [
        ("привет_я_мелвин.wav", 2.5),
        ("готов_к_работе.wav", 2.0),
        ("батарея_заряжена.wav", 2.0),
        ("подключение_установлено.wav", 2.5),
        ("ошибка_проверьте_соединение.wav", 3.0)
    ]
    
    sample_rate = 16000
    
    for filename, duration in phrases:
        # Создаем уникальный сигнал для каждой фразы
        with wave.open(filename, 'w') as wav_file:
            wav_file.setnchannels(1)
            wav_file.setsampwidth(2)
            wav_file.setframerate(sample_rate)
            
            num_samples = int(duration * sample_rate)
            amplitude = 32767 * 0.3
            
            for i in range(num_samples):
                t = i / sample_rate
                
                # Разные частоты для разных "слов"
                if t < duration * 0.3:
                    freq = 300 + 100 * math.sin(2 * math.pi * 3 * t)  # Первое "слово"
                elif t < duration * 0.6:
                    freq = 400 + 50 * math.sin(2 * math.pi * 5 * t)   # Второе "слово"
                else:
                    freq = 350 + 80 * math.sin(2 * math.pi * 4 * t)   # Третье "слово"
                
                value = amplitude * math.sin(2 * math.pi * freq * t)
                
                # Огибающая
                envelope = 1.0
                if t < 0.1:
                    envelope = t / 0.1
                elif t > duration - 0.1:
                    envelope = (duration - t) / 0.1
                
                value *= envelope
                packed_value = struct.pack('<h', int(value))
                wav_file.writeframes(packed_value)
        
        print(f"  Создан: {filename} (имитация речи)")

def main():
    print("Создание тестовых WAV файлов для робота Мелвина")
    print("=" * 60)
    
    # Создаем директорию для WAV файлов, если её нет
    if not os.path.exists("wav_files"):
        os.makedirs("wav_files")
    
    print("\n1. Базовые тестовые файлы:")
    create_sine_wav("wav_files/hello.wav", duration=3.0, frequency=1000.0)
    create_sine_wav("wav_files/test_tone_440.wav", duration=2.0, frequency=440.0)
    create_chirp_wav("wav_files/chirp.wav")
    create_beep_sequence("wav_files/beeps.wav")
    
    print("\n2. Голосоподобные сигналы:")
    create_voice_like_wav("wav_files/voice_like.wav")
    
    print("\n3. Имитация TTS фраз для Мелвина:")
    os.chdir("wav_files")
    create_tts_test_files()
    os.chdir("..")
    
    print("\n" + "=" * 60)
    print("Все файлы созданы в директории 'wav_files/'")
    print("\nИнструкция:")
    print("1. Скопируйте WAV файлы в корень SD карты")
    print("2. Для теста TTS создайте директорию /tts/ на SD карте")
    print("3. Скопируйте файлы из wav_files/tts/ в /tts/ на SD карте")
    print("4. Вставьте SD карту в плату SpotPear ESP32-S3")
    print("5. Загрузите прошивку и наслаждайтесь звуком!")
    
    print("\nСодержимое директории wav_files/:")
    for file in os.listdir("wav_files"):
        size = os.path.getsize(os.path.join("wav_files", file))
        print(f"  {file:30} {size:8} байт")

if __name__ == "__main__":
    main()