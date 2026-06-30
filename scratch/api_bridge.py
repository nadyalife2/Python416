import http.server
import os
import subprocess
import tempfile
import sys

def setup_firewall(port):
    if os.name != 'nt':
        return
    rule_name = "Melvin API Bridge"
    try:
        check_cmd = f'powershell -Command "Get-NetFirewallRule -DisplayName \'{rule_name}\' -ErrorAction Stop"'
        result = subprocess.run(check_cmd, shell=True, capture_output=True)
        if result.returncode == 0:
            return
    except Exception:
        pass

    print(f"[BRIDGE] Requesting Administrator privileges to open port {port} in Windows Firewall...")
    add_cmd = f"New-NetFirewallRule -DisplayName '{rule_name}' -Direction Inbound -Action Allow -Protocol TCP -LocalPort {port}"
    elevate_cmd = f'powershell -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell -ArgumentList \\"-NoProfile -ExecutionPolicy Bypass -Command {add_cmd}\\" -Verb RunAs"'
    try:
        subprocess.run(elevate_cmd, shell=True)
        print("[BRIDGE] UAC prompt triggered. Please click 'Yes' to allow Melvin to connect.")
    except Exception as e:
        print(f"[BRIDGE] Failed to add firewall rule automatically: {e}")
        print(f"Please run this manually as Admin: {add_cmd}")


class BridgeHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print(f"[BRIDGE] {self.address_string()} - {format % args}")

    def do_POST(self):
        self.handle_request("POST")

    def do_GET(self):
        self.handle_request("GET")

    def handle_request(self, method):
        path = self.path
        if path.startswith("/groq/"):
            target_url = "https://api.groq.com/" + path[len("/groq/"):]
        elif path.startswith("/gemini/"):
            target_url = "https://generativelanguage.googleapis.com/" + path[len("/gemini/"):]
        elif path.startswith("/openrouter/"):
            target_url = "https://openrouter.ai/" + path[len("/openrouter/"):]
        elif path.startswith("/google-tts/"):
            target_url = "https://texttospeech.googleapis.com/" + path[len("/google-tts/"):]
        elif path.startswith("/openai-tts/"):
            target_url = "https://api.openai.com/" + path[len("/openai-tts/"):]
        elif path.startswith("/elevenlabs-tts/"):
            target_url = "https://api.elevenlabs.io/" + path[len("/elevenlabs-tts/"):]
        elif path.startswith("/yandex-tts/"):
            target_url = "https://tts.api.cloud.yandex.net/" + path[len("/yandex-tts/"):]
        elif path.startswith("/yandex-stt/"):
            target_url = "https://stt.api.cloud.yandex.net/" + path[len("/yandex-stt/"):]
        elif path.startswith("/yandex-gpt/"):
            target_url = "https://llm.api.cloud.yandex.net/" + path[len("/yandex-gpt/"):]
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b"Not Found")
            return

        # Read content length
        content_length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(content_length) if content_length > 0 else None

        # Prepare request headers (exclude Host)
        headers = {k: v for k, v in self.headers.items() if k.lower() != 'host'}

        if path.startswith("/google-tts/"):
            import json, base64
            try:
                body_str = body.decode('utf-8', errors='ignore')
                import json
                try:
                    data = json.loads(body_str)
                    text = data.get("input", {}).get("text", "")
                except Exception:
                    start = body_str.find('"text":')
                    if start != -1:
                        start = body_str.find('"', start + 7) + 1
                        end = body_str.rfind('"')
                        if end > start:
                            text = body_str[start:end].replace('\\"', '"').replace('\\n', ' ')
                        else:
                            text = ""
                    else:
                        text = ""
                if not text:
                    text = "Текст пуст."
                print(f"[BRIDGE] FREE TTS: Synthesizing '{text}'")
                temp_mp3 = tempfile.mktemp(suffix=".mp3")
                temp_wav = tempfile.mktemp(suffix=".wav")
                subprocess.run(["edge-tts", "--text", text, "--voice", "ru-RU-SvetlanaNeural", "--write-media", temp_mp3], check=True)
                subprocess.run(["ffmpeg", "-y", "-i", temp_mp3, "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", temp_wav], check=True, capture_output=True)
                with open(temp_wav, "rb") as f:
                    wav_data = f.read()
                b64_audio = base64.b64encode(wav_data).decode('utf-8')
                if os.path.exists(temp_mp3): os.remove(temp_mp3)
                if os.path.exists(temp_wav): os.remove(temp_wav)
                resp_json = json.dumps({"audioContent": b64_audio})
                self.send_response(200)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(resp_json)))
                self.end_headers()
                self.wfile.write(resp_json.encode('utf-8'))
                return
            except Exception as e:
                print(f"[BRIDGE] FREE TTS ERROR: {e}")
                self.send_response(500)
                self.end_headers()
                self.wfile.write(f"Free TTS Error: {e}".encode('utf-8'))
                return

        temp_file = None
        if body:
            temp_dir = os.path.dirname(os.path.abspath(__file__))
            fd, temp_file_path = tempfile.mkstemp(dir=temp_dir, suffix=".bin")
            with os.fdopen(fd, 'wb') as f:
                f.write(body)
            temp_file = temp_file_path

        cmd = ["curl.exe", "-s", "-i", "-X", method, target_url]
        for k, v in headers.items():
            if k.lower() == 'content-length':
                continue
            cmd.extend(["-H", f"{k}: {v}"])

        cmd.extend(["-H", "Expect:"])

        if temp_file:
            cmd.extend(["--data-binary", f"@{temp_file}"])

        try:
            print(f"[BRIDGE] Relaying request to {target_url} using curl.exe...")
            print(f"[BRIDGE] Command: {' '.join(cmd[:10])} ...") # Limit output print
            
            max_retries = 3
            for attempt in range(max_retries):
                res = subprocess.run(cmd, capture_output=True, timeout=45)
                if res.returncode == 0 or attempt == max_retries - 1:
                    break
                print(f"[BRIDGE] curl.exe failed with code {res.returncode}. Retrying ({attempt+1}/{max_retries})...")
                import time
                time.sleep(1)
            
            # Clean up temp file
            if temp_file and os.path.exists(temp_file):
                os.remove(temp_file)

            if res.returncode != 0:
                stderr_msg = res.stderr.decode('utf-8', errors='ignore')
                print(f"[BRIDGE] curl.exe failed with exit code {res.returncode}: {stderr_msg}")
                self.send_response(502)
                self.end_headers()
                self.wfile.write(f"Bridge Error: curl.exe failed: {stderr_msg}".encode('utf-8'))
                return

            stdout = res.stdout
            if len(stdout) == 0:
                stderr_msg = res.stderr.decode('utf-8', errors='ignore')
                print(f"[BRIDGE] curl.exe returned empty stdout. Stderr: {stderr_msg}")
                self.send_response(502)
                self.end_headers()
                self.wfile.write(f"Bridge Error: curl.exe returned empty stdout. Stderr: {stderr_msg}".encode('utf-8'))
                return

            parts = stdout.split(b"\r\n\r\n")
            if len(parts) < 2:
                parts = stdout.split(b"\n\n")

            header_block = None
            body_block = b""
            
            final_header_idx = -1
            for i, part in enumerate(parts):
                if part.startswith(b"HTTP/1.1 ") or part.startswith(b"HTTP/2 ") or part.startswith(b"HTTP/1.0 "):
                    if b"100 Continue" not in part:
                        final_header_idx = i
            
            if final_header_idx != -1:
                header_block = parts[final_header_idx]
                body_block = b"\r\n\r\n".join(parts[final_header_idx + 1:])
            else:
                header_block = parts[0]
                body_block = b"\r\n\r\n".join(parts[1:])

            # Parse status code
            status_code = 200
            headers_to_send = []
            if header_block:
                lines = header_block.split(b"\n")
                first_line = lines[0].decode('utf-8', errors='ignore').strip()
                if first_line.startswith("HTTP/"):
                    status_code = int(first_line.split()[1])
                for line in lines[1:]:
                    line = line.strip()
                    if not line:
                        continue
                    colon = line.find(b":")
                    if colon != -1:
                        k = line[:colon].decode('utf-8', errors='ignore').strip()
                        v = line[colon+1:].decode('utf-8', errors='ignore').strip()
                        if k.lower() not in ('transfer-encoding', 'connection', 'content-length'):
                            headers_to_send.append((k, v))

            print(f"[BRIDGE] Received response with status {status_code}, forwarding...")
            self.send_response(status_code)
            for k, v in headers_to_send:
                self.send_header(k, v)
            self.send_header("Content-Length", str(len(body_block)))
            self.end_headers()
            self.wfile.write(body_block)

        except Exception as e:
            if temp_file and os.path.exists(temp_file):
                os.remove(temp_file)
            print(f"[BRIDGE] Error: {e}")
            self.send_response(500)
            self.end_headers()
            self.wfile.write(f"Bridge Error: {str(e)}".encode('utf-8'))

def run(port=8080):
    setup_firewall(port)
    server_address = ('', port)
    httpd = http.server.HTTPServer(server_address, BridgeHandler)
    print(f"[BRIDGE] Starting API bridge on port {port}...")
    print(f"[BRIDGE] Routing:")
    print(f"  http://localhost:{port}/groq/ -> https://api.groq.com/")
    print(f"  http://localhost:{port}/gemini/ -> https://generativelanguage.googleapis.com/")
    print(f"  http://localhost:{port}/openrouter/ -> https://openrouter.ai/")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("[BRIDGE] Stopping server...")
        httpd.server_close()

if __name__ == '__main__':
    run()
