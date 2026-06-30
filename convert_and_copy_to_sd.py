#!/usr/bin/env python3
"""
Конвертирует MP3 из wav_files/ в WAV (16kHz mono 16bit)
и копирует их на SD карту.
Требует: pip install pydub
И FFmpeg: https://ffmpeg.org/download.html (или: winget install ffmpeg)
"""

import os
import sys
import shutil
import subprocess

SRC_DIR = "wav_files"

def find_sd_drive():
    """Ищем SD карту среди дисков"""
    import string
    for letter in string.ascii_uppercase:
        drive = f"{letter}:\\"
        if os.path.exists(drive) and letter not in ("C", "D", "E"):
            return drive
    return None

def convert_mp3_to_wav(src, dst):
    """Конвертирует через ffmpeg в PCM 16kHz mono 16bit WAV"""
    cmd = [
        "ffmpeg", "-y",
        "-i", src,
        "-ar", "16000",   # 16kHz
        "-ac", "1",       # mono
        "-sample_fmt", "s16",  # 16bit
        dst
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  ОШИБКА ffmpeg: {result.stderr[-200:]}")
        return False
    return True

def main():
    print("=" * 60)
    print("Мелвин — конвертатор MP3 → WAV и копировщик на SD")
    print("=" * 60)

    # Проверяем ffmpeg
    if shutil.which("ffmpeg") is None:
        print("\n❌ ffmpeg не найден!")
        print("   Установи: winget install ffmpeg")
        print("   Или скачай с https://ffmpeg.org/download.html")
        sys.exit(1)
    print("✅ ffmpeg найден")

    # Создаём папку converted/
    converted_dir = "wav_files_converted"
    os.makedirs(converted_dir, exist_ok=True)

    # Конвертируем все MP3
    print(f"\nКонвертирую из {SRC_DIR}/ → {converted_dir}/")
    converted = []
    for fname in sorted(os.listdir(SRC_DIR)):
        src_path = os.path.join(SRC_DIR, fname)
        name_no_ext = os.path.splitext(fname)[0]
        ext = os.path.splitext(fname)[1].lower()

        if ext in (".mp3", ".wav", ".ogg", ".m4a"):
            dst_name = name_no_ext + ".wav"
            dst_path = os.path.join(converted_dir, dst_name)
            print(f"  {fname} → {dst_name} ... ", end="", flush=True)
            if convert_mp3_to_wav(src_path, dst_path):
                size = os.path.getsize(dst_path)
                print(f"OK ({size//1024} KB)")
                converted.append((dst_name, dst_path))
            else:
                print("ОШИБКА")

    if not converted:
        print("\nНет файлов для копирования!")
        return

    # Ищем SD карту
    print(f"\nИщу SD карту...")
    sd = find_sd_drive()
    if sd:
        print(f"✅ SD карта найдена: {sd}")
        answer = input(f"Копировать {len(converted)} WAV файлов на {sd}? [y/N]: ")
        if answer.lower() == 'y':
            for dst_name, dst_path in converted:
                target = os.path.join(sd, dst_name)
                shutil.copy2(dst_path, target)
                print(f"  → {target}")
            print(f"\n✅ Скопировано {len(converted)} файлов на SD!")
            print("\nФайлы на SD карте теперь:")
            for f in sorted(os.listdir(sd)):
                if f.endswith(".wav"):
                    size = os.path.getsize(os.path.join(sd, f))
                    print(f"  {f:40} {size//1024} KB")
        else:
            print("Отмена. WAV файлы готовы в", converted_dir)
    else:
        print("⚠️  SD карта не найдена автоматически.")
        print(f"   Скопируй вручную из '{converted_dir}/' в корень SD карты.")
        print("\nГотовые WAV файлы:")
        for dst_name, dst_path in converted:
            size = os.path.getsize(dst_path)
            print(f"  {dst_path:50} {size//1024} KB")

    print("\n📋 Что должно быть в корне SD карты:")
    print("  hello.wav         ← приветствие при старте")
    print("  ready.wav         ← готов к работе")
    print("  wifi_ok.wav       ← WiFi подключён")
    print("  error.wav         ← ошибка")
    print("  config.json       ← создаётся через веб-панель (192.168.4.1)")

if __name__ == "__main__":
    main()
