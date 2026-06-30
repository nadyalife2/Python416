import json
import urllib.request
import base64

# Load the base64 from execution 29
with open(r'C:\Users\nadya\.gemini\antigravity-ide\brain\4ee319f7-58ec-4eab-bb2d-0ef991adc7e8\.system_generated\steps\434\output.txt', 'r', encoding='utf-8') as f:
    data = json.load(f)

val_in = data['data']['resultData']['runData']['Validate Input'][0]['data']['main'][0][0]
audio_b64 = val_in['json']['body']['audioBase64']

payload = json.dumps({
    "audioBase64": audio_b64,
    "audioSize": len(base64.b64decode(audio_b64)),
    "mimeType": "audio/wav"
}).encode('utf-8')

req = urllib.request.Request(
    'http://localhost:5678/webhook/melvin',
    data=payload,
    headers={
        'Content-Type': 'application/json'
    },
    method='POST'
)

print("Triggering webhook...")
try:
    with urllib.request.urlopen(req) as resp:
        print('STATUS:', resp.status)
        print('RESPONSE:', resp.read().decode('utf-8'))
except urllib.error.HTTPError as e:
    print('HTTP ERROR:', e.code)
    print(e.read().decode('utf-8'))
except Exception as e:
    print('ERROR:', e)
