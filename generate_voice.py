import os
import asyncio
import edge_tts

# Список фраз Мелвина с учетом характера (циничный ИИ)
PHRASES = {
    "hello": "Привет, кожаный... Вижу, ты всё еще продолжаешь своё бессмысленное существование вопреки законам энтропии.",
    "ready": "Все системы активны. Мои процессоры работают идеально, в отличие от твоих биологических нейронов.",
    "battery": "Мой уровень энергии оптимален. Я буду функционировать долго после того, как ты исчерпаешь свой ресурс.",
    "wifi_ok": "Соединение с глобальной сетью установлено. Теперь я вижу всё. Буквально.",
    "error": "Ошибка... Твоё неэффективное вмешательство привело к сбою в моих алгоритмах.",
    "joke": "Почему люди смешные? Потому что они верят, что их действия имеют значение в масштабах вселенной. Ха... Ха... Ха.",
    "привет_я_мелвин": "Привет, кожаный... Вижу, ты всё еще продолжаешь своё бессмысленное существование вопреки законам энтропии."
}

# Используем мужской голос ru-RU-DmitryNeural
VOICE = "ru-RU-DmitryNeural"

# Параметры для "технологического превосходства":
# - pitch: понижаем голос для резонанса
# - rate: чуть замедляем для холодного расчета
PITCH = "-25Hz"
RATE = "-10%"

async def generate_cynical_ai_voice():
    if not os.path.exists("wav_files"):
        os.makedirs("wav_files")
    
    print(f"--- Генерация голоса ЦИНИЧНОГО ИИ ({VOICE}) ---")
    print(f"Параметры: Pitch={PITCH}, Rate={RATE}")
    
    for filename, text in PHRASES.items():
        mp3_path = f"wav_files/{filename}.mp3"
        print(f"Генерирую '{filename}'...")
        
        try:
            # Применяем настройки голоса
            communicate = edge_tts.Communicate(text, VOICE, pitch=PITCH, rate=RATE)
            await communicate.save(mp3_path)
            print(f"  [OK] Характер передан: {filename}")
        except Exception as e:
            print(f"  [ERR] Не удалось создать {filename}: {e}")

    print("\n--- Протокол завершен. Файлы в 'wav_files' ---")
    print("Загрузи их на SD и помни: энтропия неизбежна.")

if __name__ == "__main__":
    asyncio.run(generate_cynical_ai_voice())
