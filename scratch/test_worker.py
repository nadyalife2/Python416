import urllib.request
import urllib.error
import json
import sys
import os

def test_proxy():
    proxy_url = "https://throbbing-snowflake-5897.hollabaughcahoon.workers.dev/groq/openai/v1/chat/completions"
    api_key = os.environ.get("GROQ_API_KEY", "YOUR_GROQ_API_KEY")
    headers = {
        "Content-Type": "application/json",
        "Authorization": f"Bearer {api_key}",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
    }
    
    payload = {
        "model": "llama-3.3-70b-versatile",
        "messages": [
            {"role": "user", "content": "Hello! Respond with exactly the word 'OK'."}
        ]
    }
    
    data = json.dumps(payload).encode('utf-8')
    req = urllib.request.Request(proxy_url, data=data, headers=headers, method="POST")
    
    print(f"[TEST] Sending test request to Cloudflare Worker proxy: {proxy_url}...")
    try:
        with urllib.request.urlopen(req, timeout=10) as res:
            print(f"[TEST] Success! Response status: {res.status}")
            body = res.read().decode('utf-8')
            print(f"[TEST] Response body: {body}")
    except urllib.error.HTTPError as e:
        print(f"[TEST] HTTP Error: {e.code}", file=sys.stderr)
        print(f"[TEST] Response: {e.read().decode('utf-8')}", file=sys.stderr)
    except Exception as e:
        print(f"[TEST] General Error: {e}", file=sys.stderr)

if __name__ == "__main__":
    test_proxy()
