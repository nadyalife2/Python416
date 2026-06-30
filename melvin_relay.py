# -*- coding: utf-8 -*-
"""
Melvin Local Relay v4
Принимает запрос от робота (ESP32) и пересылает в n8n Cloud через Python (обход Cloudflare JA3).
Поддерживает: JSON base64 (новый формат) и multipart/form-data (старый формат).
"""
import sys
import base64
import json
import socket
import requests
from http.server import HTTPServer, BaseHTTPRequestHandler

N8N_WEBHOOK = "https://egoya2023.app.n8n.cloud/webhook/melvin"
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

        # --- Определяем формат: новый JSON base64 или старый multipart ---
        if "application/json" in content_type:
            # Новый формат: робот уже прислал {"audioBase64":"...","audioSize":N}
            try:
                parsed = json.loads(body)
                audio_b64 = parsed.get("audioBase64", "")
                audio_size = parsed.get("audioSize", 0)
                mime = parsed.get("mimeType", "audio/wav")
                print(f"[RELAY] JSON mode: audioSize={audio_size}, b64 len={len(audio_b64)}", flush=True)
            except Exception as e:
                print(f"[RELAY] JSON parse error: {e}", flush=True)
                self.send_response(400)
                self.end_headers()
                self.wfile.write(b"Bad JSON")
                return
            payload = json.dumps({
                "audioBase64": audio_b64,
                "audioSize": audio_size,
                "mimeType": mime
            })
        else:
            # Старый формат: multipart/form-data
            audio_bytes = extract_audio_from_multipart(body, content_type)
            audio_b64 = base64.b64encode(audio_bytes).decode("utf-8")
            print(f"[RELAY] Multipart mode: {len(audio_bytes)} bytes -> b64 {len(audio_b64)} chars", flush=True)
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
            
            # Extract text from n8n response — handles dict OR list format
            text_to_speak = ""
            try:
                n8n_data = resp.json()
                print(f"[RELAY] n8n raw response: {json.dumps(n8n_data, ensure_ascii=False)[:300]}", flush=True)
                if isinstance(n8n_data, list):
                    item = n8n_data[0] if n8n_data else {}
                    if isinstance(item, dict):
                        text_to_speak = item.get("text_spoken") or item.get("text") or ""
                    else:
                        text_to_speak = str(item)
                elif isinstance(n8n_data, dict):
                    text_to_speak = n8n_data.get("text_spoken") or n8n_data.get("text") or ""
                else:
                    text_to_speak = str(n8n_data)
            except Exception as e:
                print(f"[RELAY] JSON parse error: {e}. Raw content: {resp.content[:300]}", flush=True)
                text_to_speak = ""

            # Ensure text_to_speak is a string and handle potential list inside dict values
            if isinstance(text_to_speak, list):
                text_to_speak = " ".join(str(x) for x in text_to_speak)
            elif not isinstance(text_to_speak, str):
                text_to_speak = str(text_to_speak)
                
            print(f"[RELAY] text_to_speak stringified: {text_to_speak[:80]}", flush=True)

            if text_to_speak:
                # Возвращаем роботу JSON с полем text_spoken — робот сам озвучит через TTS
                response_json = json.dumps({"text_spoken": text_to_speak}, ensure_ascii=False).encode("utf-8")
                print(f"[RELAY] Returning JSON to robot: text_spoken len={len(text_to_speak)}", flush=True)
                self.send_response(200)
                self.send_header("Content-Type", "application/json; charset=utf-8")
                self.send_header("Content-Length", str(len(response_json)))
                self.end_headers()
                self.wfile.write(response_json)


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
    # Автоматически узнаём локальный IP
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
    except Exception:
        local_ip = "127.0.0.1"
    print(f"[RELAY] Melvin Local Relay v4", flush=True)
    print(f"[RELAY] N8N Webhook: {N8N_WEBHOOK}", flush=True)
    print(f"[RELAY] Starting on http://0.0.0.0:{LISTEN_PORT}", flush=True)
    print(f"[RELAY] >>> Set Melvin api_proxy to: http://{local_ip}:{LISTEN_PORT} <<<", flush=True)
    server = HTTPServer(("0.0.0.0", LISTEN_PORT), RelayHandler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
