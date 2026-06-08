import http.server
import urllib.request
import urllib.error
import ssl
import os
import subprocess

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
    # Disable SSL verification for simple local requests if needed
    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE

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

        req = urllib.request.Request(target_url, data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(req, context=self.ctx, timeout=35) as res:
                self.send_response(res.status)
                for k, v in res.getheaders():
                    if k.lower() not in ('transfer-encoding', 'connection'):
                        self.send_header(k, v)
                self.end_headers()
                self.wfile.write(res.read())
        except urllib.error.HTTPError as e:
            self.send_response(e.code)
            for k, v in e.headers.items():
                if k.lower() not in ('transfer-encoding', 'connection'):
                    self.send_header(k, v)
            self.end_headers()
            self.wfile.write(e.read())
        except Exception as e:
            self.send_response(500)
            self.end_headers()
            self.wfile.write(str(e).encode('utf-8'))

def run(port=8080):
    setup_firewall(port)
    server_address = ('', port)
    httpd = http.server.HTTPServer(server_address, BridgeHandler)
    print(f"[BRIDGE] Starting API bridge on port {port}...")
    print(f"[BRIDGE] Routing:")
    print(f"  http://localhost:{port}/groq/ -> https://api.groq.com/")
    print(f"  http://localhost:{port}/gemini/ -> https://generativelanguage.googleapis.com/")
    print(f"  http://localhost:{port}/openrouter/ -> https://openrouter.ai/")
    print(f"  http://localhost:{port}/google-tts/ -> https://texttospeech.googleapis.com/")
    print(f"  http://localhost:{port}/openai-tts/ -> https://api.openai.com/")
    print(f"  http://localhost:{port}/elevenlabs-tts/ -> https://api.elevenlabs.io/")
    print(f"  http://localhost:{port}/yandex-tts/ -> https://tts.api.cloud.yandex.net/")
    print(f"  http://localhost:{port}/yandex-stt/ -> https://stt.api.cloud.yandex.net/")
    print(f"  http://localhost:{port}/yandex-gpt/ -> https://llm.api.cloud.yandex.net/")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("[BRIDGE] Stopping server...")
        httpd.server_close()

if __name__ == '__main__':
    run()
