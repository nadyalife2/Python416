import requests
import json
import sys

sys.stdout.reconfigure(encoding='utf-8')

def check(query, persona=None):
    payload = {"message": query}
    if persona:
        payload["persona"] = persona
    r = requests.post("http://127.0.0.1:5000/chat", json=payload, timeout=30)
    data = r.json()
    resp = data.get("response", "")
    p = data.get("persona", "")
    trigger = data.get("trigger_voice_record", False)
    tool = data.get("tool_used", "")
    print(f"[{p} | tool: {tool} | auto_rec: {trigger}] Q: '{query}'")
    print(f"-> {resp}\n")
    return data

print("=== 1. WAKE-WORD 'Мелвин' (ДОЛЖЕН ЗАПУСТИТЬ ЗАПИСЬ ГОЛОСА) ===")
check("Мелвин")

print("=== 2. WAKE-WORD 'Рик' (ДОЛЖЕН ПЕРЕКЛЮЧИТЬ НА РИКА И ЗАПУСТИТЬ ЗАПИСЬ) ===")
check("Рик")

print("=== 3. РИК ВСЕГДА ДОЛЖЕН НАЧИНАТЬ С *рыгает* ===")
check("Рик, посвети фонариком")
check("Сколько заряда батареи?")
check("Какая погода?")
check("Как твои дела?")
check("Рик, кто такой Морти?")
