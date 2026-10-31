import requests
import json
import time
import sys

sys.stdout.reconfigure(encoding='utf-8')

SYSTEM_RICK_STRICT = (
    "Ты — Рик Санчез (дубляж Сыендука), бортовой ИИ Мелвин на старом Xiaomi Redmi Note 4. "
    "Ты гений, презирающий людей и глупые вопросы. Отвечай злобно, едко, саркастично, в 1 короткое хлесткое предложение."
)

FEW_SHOT = [
    {"role": "user", "content": "Привет"},
    {"role": "assistant", "content": "*рыгает* Чего пялишься? Я занят выжиманием соков из этого хлама, а ты тратишь мое время."},
    {"role": "user", "content": "Кто ты?"},
    {"role": "assistant", "content": "Я Мелвин — величайший ум галактики в китайском ведре с болтами. Не беси меня."}
]

def ask_rick(msg, prefix="*рыгает* "):
    prompt = f"<|im_start|>system\n{SYSTEM_RICK_STRICT}<|im_end|>\n"
    for item in FEW_SHOT:
        prompt += f"<|im_start|>{item['role']}\n{item['content']}<|im_end|>\n"
    prompt += f"<|im_start|>user\n{msg}<|im_end|>\n<|im_start|>assistant\n{prefix}"
    
    payload = {
        "prompt": prompt,
        "n_predict": 25,
        "temperature": 0.85,
        "top_p": 0.9,
        "presence_penalty": 0.6,
        "frequency_penalty": 0.6,
        "stop": ["<|im_end|>", "<|im_start|>", "\nuser", "\nUser:", "\nПользователь:"]
    }
    t0 = time.time()
    r = requests.post("http://127.0.0.1:8080/completion", json=payload, timeout=60)
    dt = round(time.time() - t0, 2)
    res = r.json()
    content = prefix + res.get("content", "").strip()
    timings = res.get("timings", {})
    print(f"[{dt}s] (eval: {timings.get('prompt_ms', 0):.0f}ms / gen: {timings.get('predicted_ms', 0):.0f}ms)")
    print(f"Ответ: {content}\n")

print("--- Test 1 ---")
ask_rick("Чем займемся сегодня?", prefix="*рыгает* ")

print("--- Test 2 ---")
ask_rick("Ты готов к работе?", prefix="Слушай сюда, ")

print("--- Test 3 ---")
ask_rick("Как настроение?", prefix="*рыгает* О боже, ")
