import socket

host = "throbbing-snowflake-5897.hollabaughcahoon.workers.dev"
port = 80
path = "/groq/openai/v1/audio/transcriptions"
key = "YOUR_GROQ_API_KEY"
boundary = "----MelvinBoundary123456789"
contentType = f"multipart/form-data; boundary={boundary}"

header = (
    f"--{boundary}\r\n"
    "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
    "whisper-large-v3-turbo\r\n"
    f"--{boundary}\r\n"
    "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
    "Content-Type: audio/wav\r\n\r\n"
)
footer = f"\r\n--{boundary}--\r\n"
file_data = b"\x00" * 98000
payload_len = len(header.encode('utf-8')) + len(file_data) + len(footer.encode('utf-8'))

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((host, port))

req = (
    f"POST {path} HTTP/1.1\r\n"
    f"Host: {host}\r\n"
    "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36\r\n"
    f"Authorization: Bearer {key}\r\n"
    f"Content-Type: {contentType}\r\n"
    f"Content-Length: {payload_len}\r\n"
    "Connection: close\r\n\r\n"
)

s.sendall(req.encode('utf-8'))
print("Headers sent. Now sending body...")
s.sendall(header.encode('utf-8'))
s.sendall(file_data)
s.sendall(footer.encode('utf-8'))

print("Body sent. Reading response...")
resp = b""
while True:
    chunk = s.recv(4096)
    if not chunk:
        break
    resp += chunk
print(resp.decode('utf-8', errors='ignore'))
