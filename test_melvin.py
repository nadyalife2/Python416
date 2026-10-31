import sys
import urllib.request
import json
import time

sys.stdout.reconfigure(encoding='utf-8')

url = "http://192.168.31.48:5000/chat"
payload = {
    "message": "Ты помнишь кодек ES8311 в нашем роботе?"
}

print(f"Отправка запроса Мелвину на смартфон (192.168.31.48:5000)...")
t0 = time.time()
req = urllib.request.Request(
    url, 
    data=json.dumps(payload).encode('utf-8'),
    headers={"Content-Type": "application/json"}
)

try:
    with urllib.request.urlopen(req, timeout=120) as response:
        res = json.loads(response.read().decode('utf-8'))
        dt = round(time.time() - t0, 2)
        print(f"\n=== ОТВЕТ МЕЛВИНА (время: {dt} сек) ===")
        print(f"Ответ: {res.get('response')}")
        print(f"Вспомнил воспоминаний: {res.get('memories_recalled')}")
except Exception as e:
    print(f"Ошибка запроса: {e}")
