#ifndef WEBUI_H
#define WEBUI_H

const char WIFI_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>Melvin Control Panel</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; text-align: center; background: #121212; color: white; padding: 20px; }
    .card { max-width: 400px; margin: auto; background: #1e1e1e; padding: 20px; border-radius: 15px; box-shadow: 0 10px 20px rgba(0,0,0,0.5); }
    h2 { color: #00e5ff; margin-bottom: 20px; }
    input, select, textarea { width: 90%; padding: 12px; margin: 10px 0; border: none; border-radius: 8px; background: #2c2c2c; color: white; font-size: 14px; }
    button { width: 95%; padding: 15px; background: #00e5ff; border: none; border-radius: 8px; color: #121212; font-weight: bold; cursor: pointer; margin-top: 10px; }
    button:hover { background: #00b8d4; }
    .status { margin-top: 15px; font-size: 14px; color: #00ff00; }
  </style>
</head>
<body>
  <div class="card">
    <h2>Melvin v7.0 Setup</h2>
    <input type="text" id="ssid" placeholder="WiFi Name (SSID)">
    <input type="password" id="pass" placeholder="WiFi Password">
    <hr style="border:0.5px solid #333">
    <input type="text" id="gemini_keys" placeholder="Gemini API Keys (key1,key2...)">
    <input type="text" id="tts_key" placeholder="Google TTS API Key">
    <select id="personality">
        <option value="rick">Rick Sanchez Style</option>
        <option value="calm">Calm Assistant</option>
        <option value="podcast">Podcast Mode</option>
    </select>
    <input type="text" id="wake_word" placeholder="Wake Word (e.g. Мелвин)">
    <button onclick="save()">SAVE & REBOOT</button>
    <div id="status" class="status"></div>
  </div>

  <script>
    function load() {
      fetch('/api/config').then(r => r.json()).then(data => {
        document.getElementById('ssid').value = data.wifi_ssid || '';
        document.getElementById('gemini_keys').value = data.gemini_keys || '';
        document.getElementById('tts_key').value = data.tts_key || '';
        document.getElementById('personality').value = data.personality || 'rick';
        document.getElementById('wake_word').value = data.wake_word || 'Мелвин';
      });
    }
    function save() {
      const config = {
        wifi_ssid: document.getElementById('ssid').value,
        wifi_pass: document.getElementById('pass').value,
        gemini_keys: document.getElementById('gemini_keys').value,
        tts_key: document.getElementById('tts_key').value,
        personality: document.getElementById('personality').value,
        wake_word: document.getElementById('wake_word').value
      };
      document.getElementById('status').innerText = 'Saving to SD... Rebooting...';
      fetch('/api/save', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
      }).then(() => {
        setTimeout(() => alert('Melvin is restarting! Check Serial Monitor.'), 1000);
      });
    }
    window.onload = load;
  </script>
</body>
</html>
)rawliteral";

#endif
