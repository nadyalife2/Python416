import urllib.request
import json
import time

url = "http://192.168.31.48:8080/completion"
payload = {
    "prompt": "Hello! Please reply in one word:",
    "n_predict": 8,
    "temperature": 0.2
}

print("Testing raw llama-server generation...")
t0 = time.time()
req = urllib.request.Request(
    url,
    data=json.dumps(payload).encode('utf-8'),
    headers={"Content-Type": "application/json"}
)

try:
    with urllib.request.urlopen(req, timeout=60) as resp:
        data = json.loads(resp.read().decode('utf-8'))
        dt = round(time.time() - t0, 2)
        print(f"Time: {dt}s")
        print(f"Content: {data.get('content')}")
        print(f"Tokens predicted: {data.get('tokens_predicted')}")
        print(f"Timings: {data.get('timings')}")
except Exception as e:
    print(f"Error: {e}")
