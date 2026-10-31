import requests
import json
import sys
import time

sys.stdout.reconfigure(encoding='utf-8')

def chat(msg, persona=None):
    t0 = time.time()
    payload = {"message": msg}
    if persona:
        payload["persona"] = persona
    r = requests.post("http://127.0.0.1:5000/chat", json=payload, timeout=45)
    dt = round(time.time() - t0, 2)
    data = r.json()
    resp = data.get("response", "")
    tool = data.get("tool_used", "")
    p = data.get("persona", "")
    print(f"[{dt}s | persona: {p} | tool: {tool}] MSG: {msg}")
    print(f"-> {resp}\n")
    return data

print("=== ТЕСТ 1: МЕЛВИН (Циничный робот) ===")
chat("Мелвин", persona="melvin")
chat("Мелвин, включи фонарик", persona="melvin")
chat("Как дела?", persona="melvin")

print("=== ТЕСТ 2: ГОЛОСОВОЕ ПЕРЕКЛЮЧЕНИЕ НА РИКА ===")
chat("Включи режим Рика")

print("=== ТЕСТ 3: РИК САНЧЕЗ (Дубляж Сыендука + отрыжка) ===")
chat("Рик", persona="rick")
chat("Рик, посвети тут", persona="rick")
chat("Как дела?", persona="rick")
chat("Какая погода?", persona="rick")

print("=== ТЕСТ 4: ПЕРЕКЛЮЧЕНИЕ ОБРАТНО НА МЕЛВИНА ===")
chat("Включи режим Мелвина")
chat("Кто ты?", persona="melvin")
