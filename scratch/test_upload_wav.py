import urllib.request
import urllib.error
import sys
import os

def test_upload():
    url = "https://throbbing-snowflake-5897.hollabaughcahoon.workers.dev/groq/openai/v1/audio/transcriptions"
    
    # Create 100KB of dummy WAV data
    wav_data = b"RIFF" + b"\x00" * (100 * 1024)
    
    boundary = "----MelvinBoundary123456789"
    api_key = os.environ.get("GROQ_API_KEY", "YOUR_GROQ_API_KEY")
    headers = {
        "Content-Type": f"multipart/form-data; boundary={boundary}",
        "Authorization": f"Bearer {api_key}",
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36"
    }
    
    header = (
        f"--{boundary}\r\n"
        'Content-Disposition: form-data; name="model"\r\n\r\n'
        "whisper-large-v3-turbo\r\n"
        f"--{boundary}\r\n"
        'Content-Disposition: form-data; name="file"; filename="audio.wav"\r\n'
        "Content-Type: audio/wav\r\n\r\n"
    ).encode('utf-8')
    
    footer = f"\r\n--{boundary}--\r\n".encode('utf-8')
    
    body = header + wav_data + footer
    
    req = urllib.request.Request(url, data=body, headers=headers, method="POST")
    print(f"[TEST] Sending multipart WAV upload ({len(body)} bytes) to Worker proxy...")
    try:
        with urllib.request.urlopen(req, timeout=30) as res:
            print(f"[TEST] Success! Status: {res.status}")
            print(f"[TEST] Body: {res.read().decode('utf-8')}")
    except urllib.error.HTTPError as e:
        print(f"[TEST] HTTP Error: {e.code}", file=sys.stderr)
        print(f"[TEST] Response: {e.read().decode('utf-8')}", file=sys.stderr)
    except Exception as e:
        print(f"[TEST] Error: {e}", file=sys.stderr)

if __name__ == "__main__":
    test_upload()
