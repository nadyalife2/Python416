# -*- coding: utf-8 -*-
"""
Melvin Local Relay v3
"""
import sys
import base64
import json
import requests
from http.server import HTTPServer, BaseHTTPRequestHandler

N8N_WEBHOOK = "https://hollabaugh.app.n8n.cloud/webhook/melvin"
LISTEN_PORT = 5000


def extract_audio_from_multipart(body: bytes, content_type: str) -> bytes:
    boundary = None
    for part in content_type.split(";"):
        part = part.strip()
        if part.startswith("boundary="):
            boundary = part[9:].strip().strip('"')
            break

    if not boundary:
        print("[RELAY] No boundary found, returning raw body")
        return body

    # The actual separator in the body is "--" + boundary
    sep = ("--" + boundary).encode()
    print(f"[RELAY] Splitting by boundary: {sep[:40]}")
    parts = body.split(sep)
    print(f"[RELAY] Got {len(parts)} parts")

    for i, part in enumerate(parts[1:], 1):
        # Final boundary ends with "--"
        stripped = part.lstrip(b"\r\n")
        if stripped.startswith(b"--"):
            continue

        if b"\r\n\r\n" in part:
            _, file_data = part.split(b"\r\n\r\n", 1)
            # Strip trailing \r\n
            if file_data.endswith(b"\r\n"):
                file_data = file_data[:-2]
            print(f"[RELAY] Part {i}: extracted {len(file_data)} bytes")
            return file_data

    print("[RELAY] No audio part found in multipart!")
    return b""


class RelayHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        msg = format % args
        print(f"[RELAY] {msg}", flush=True)

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(content_length)
        content_type = self.headers.get("Content-Type", "")

        print(f"[RELAY] Received {content_length} bytes", flush=True)
        print(f"[RELAY] Content-Type: {content_type[:80]}", flush=True)

        audio_bytes = extract_audio_from_multipart(body, content_type)
        audio_b64 = base64.b64encode(audio_bytes).decode("utf-8")

        print(f"[RELAY] Audio: {len(audio_bytes)} bytes -> b64: {len(audio_b64)} chars", flush=True)

        payload = json.dumps({
            "audioBase64": audio_b64,
            "audioSize": len(audio_bytes),
            "mimeType": "audio/wav"
        })

        try:
            resp = requests.post(
                N8N_WEBHOOK,
                data=payload,
                headers={
                    "Content-Type": "application/json",
                    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
                },
                timeout=60,
            )
            
            # Force UTF-8 decoding to prevent cp1251/iso-8859-1 corruption
            resp.encoding = 'utf-8'
            
            # Check if n8n returned JSON with "text_spoken" or "text"
            try:
                n8n_data = resp.json()
                text_to_speak = n8n_data.get("text_spoken") or n8n_data.get("text") or ""
            except:
                text_to_speak = ""
                
            print(f"[RELAY] n8n returned text: {text_to_speak.encode('utf-8', 'replace').decode('utf-8')}", flush=True)
            if text_to_speak:
                # 1. Split text into chunks <= 190 chars
                import re
                import urllib.parse
                sentences = re.split(r'(?<=[.!?])\s+', text_to_speak)
                chunks = []
                cur = ""
                for s in sentences:
                    if len(cur + " " + s) <= 190:
                        cur = (cur + " " + s).strip()
                    else:
                        if cur: chunks.append(cur)
                        cur = s[:190] if len(s) > 190 else s
                if cur: chunks.append(cur)
                if not chunks: chunks.append(text_to_speak[:190])

                # 2. Download MP3 for each chunk
                mp3_data = b""
                for chunk in chunks:
                    url = f"https://translate.google.com/translate_tts?ie=UTF-8&q={urllib.parse.quote(chunk)}&tl=ru&client=tw-ob&ttsspeed=1.0"
                    tts_resp = requests.get(url, headers={"User-Agent": "Mozilla/5.0"})
                    if tts_resp.status_code == 200:
                        mp3_data += tts_resp.content

                print(f"[RELAY] Generated {len(mp3_data)} bytes of TTS audio", flush=True)

                self.send_response(200)
                self.send_header("Content-Type", "audio/mpeg")
                self.send_header("Content-Length", str(len(mp3_data)))
                self.end_headers()
                self.wfile.write(mp3_data)

            else:
                # Fallback if n8n returned raw data
                safe_text = str(resp.content[:100])
                print(f"[RELAY] n8n (no text_spoken): {resp.status_code} {safe_text}", flush=True)
                self.send_response(resp.status_code)
                for key, val in resp.headers.items():
                    if key.lower() not in ("transfer-encoding", "connection", "content-encoding"):
                        self.send_header(key, val)
                
                # IMPORTANT: Add Content-Length if missing, so ESP32 knows when to stop!
                if "Content-Length" not in resp.headers:
                    self.send_header("Content-Length", str(len(resp.content)))

                self.end_headers()
                self.wfile.write(resp.content)

        except Exception as e:
            print(f"[RELAY] Error: {e}", flush=True)
            self.send_response(502)
            self.end_headers()
            self.wfile.write(str(e).encode())

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.end_headers()
        self.wfile.write(b"Melvin Relay v3 - OK")


if __name__ == "__main__":
    local_ip = "192.168.31.123"
    print(f"[RELAY] Starting on http://0.0.0.0:{LISTEN_PORT}", flush=True)
    print(f"[RELAY] Set Melvin API Proxy to: http://{local_ip}:{LISTEN_PORT}", flush=True)
    server = HTTPServer(("0.0.0.0", LISTEN_PORT), RelayHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
