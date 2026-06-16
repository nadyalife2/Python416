#ifndef WEBUI_H
#define WEBUI_H
const char WIFI_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>Melvin Config</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0a0a0f;--card:rgba(255,255,255,0.04);--border:rgba(0,229,255,0.15);--accent:#00e5ff;--accent2:#7c3aed;--text:#e2e8f0;--muted:#64748b;--danger:#ef4444;--success:#10b981}
body{background:var(--bg);color:var(--text);font-family:system-ui,-apple-system,sans-serif;min-height:100vh;padding-bottom:80px;overflow-x:hidden}
body::before{content:"";position:fixed;top:0;left:0;right:0;height:300px;background:radial-gradient(ellipse at 50% 0%,rgba(0,229,255,0.08) 0%,transparent 70%);pointer-events:none;z-index:0}
.hero{position:relative;z-index:1;text-align:center;padding:24px 16px 8px}
.hero-title{font-size:22px;font-weight:700;background:linear-gradient(135deg,var(--accent),var(--accent2));-webkit-background-clip:text;-webkit-text-fill-color:transparent;letter-spacing:2px;margin-top:8px}
.hero-sub{font-size:11px;color:var(--muted);letter-spacing:3px;text-transform:uppercase;margin-top:2px}
svg.face{width:80px;height:80px;filter:drop-shadow(0 0 12px rgba(0,229,255,0.5))}
.tab-content{display:none;padding:12px 14px;animation:fadeIn .2s ease}
.tab-content.active{display:block}
@keyframes fadeIn{from{opacity:0;transform:translateY(6px)}to{opacity:1;transform:none}}
.card{background:var(--card);border:1px solid var(--border);border-radius:16px;padding:16px;margin-bottom:12px;backdrop-filter:blur(12px)}
.card-title{font-size:12px;font-weight:600;color:var(--accent);text-transform:uppercase;letter-spacing:1.5px;margin-bottom:12px;display:flex;align-items:center;gap:6px}
.card-title::before{content:"";display:inline-block;width:3px;height:14px;background:var(--accent);border-radius:2px}
label{display:block;font-size:11px;color:var(--muted);margin-bottom:4px;margin-top:10px;text-transform:uppercase;letter-spacing:.8px}
label:first-of-type{margin-top:0}
.inp-wrap{position:relative}
input[type=text],input[type=password],textarea,select{width:100%;background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);border-radius:10px;padding:10px 12px;color:var(--text);font-size:13px;outline:none;transition:border-color .2s,box-shadow .2s}
input:focus,textarea:focus,select:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(0,229,255,0.1)}
textarea{resize:vertical;min-height:70px;font-family:inherit}
select{-webkit-appearance:none;appearance:none;background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='8' viewBox='0 0 12 8'%3E%3Cpath d='M1 1l5 5 5-5' stroke='%2364748b' fill='none' stroke-width='2' stroke-linecap='round'/%3E%3C/svg%3E");background-repeat:no-repeat;background-position:right 12px center;padding-right:32px}
.eye-btn{position:absolute;right:10px;top:50%;transform:translateY(-50%);background:none;border:none;color:var(--muted);cursor:pointer;font-size:15px;padding:2px 4px;line-height:1}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:6px;padding:11px 20px;border-radius:10px;border:none;font-size:13px;font-weight:600;cursor:pointer;transition:all .2s;width:100%;margin-top:8px}
.btn-primary{background:linear-gradient(135deg,var(--accent),#00b4d8);color:#000}
.btn-primary:hover{opacity:.9;transform:translateY(-1px);box-shadow:0 6px 20px rgba(0,229,255,0.3)}
.btn-primary:active{transform:translateY(0)}
.btn-danger{background:linear-gradient(135deg,#ef4444,#dc2626);color:#fff}
.btn-danger:hover{opacity:.9;transform:translateY(-1px);box-shadow:0 6px 20px rgba(239,68,68,0.3)}
.btn-secondary{background:rgba(255,255,255,0.07);color:var(--text);border:1px solid var(--border)}
.btn-secondary:hover{background:rgba(255,255,255,0.12)}
.btn-sm{width:auto;padding:8px 14px;font-size:12px;margin-top:0}
.wifi-status{display:flex;align-items:center;gap:8px;padding:10px 12px;background:rgba(16,185,129,0.08);border:1px solid rgba(16,185,129,0.2);border-radius:10px;font-size:12px;color:var(--success)}
.wifi-dot{width:8px;height:8px;border-radius:50%;background:var(--success);animation:pulse 2s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.4}}
.personality-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:4px}
.pers-card{padding:12px;border-radius:12px;border:1px solid rgba(255,255,255,0.08);background:rgba(255,255,255,0.03);cursor:pointer;transition:all .2s;text-align:center}
.pers-card:hover{border-color:var(--accent);background:rgba(0,229,255,0.05)}
.pers-card.selected{border-color:var(--accent);background:rgba(0,229,255,0.08);box-shadow:0 0 12px rgba(0,229,255,0.15)}
.pers-icon{font-size:22px;margin-bottom:4px}
.pers-name{font-size:12px;font-weight:600;color:var(--text)}
.pers-desc{font-size:10px;color:var(--muted);margin-top:2px}
.stat-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.stat-card{background:rgba(255,255,255,0.04);border:1px solid var(--border);border-radius:12px;padding:14px;text-align:center}
.stat-val{font-size:20px;font-weight:700;color:var(--accent);font-variant-numeric:tabular-nums}
.stat-lbl{font-size:10px;color:var(--muted);margin-top:2px;text-transform:uppercase;letter-spacing:.8px}
.state-badge{display:inline-flex;align-items:center;gap:6px;padding:6px 12px;border-radius:20px;background:rgba(0,229,255,0.1);border:1px solid rgba(0,229,255,0.2);font-size:12px;font-weight:600;color:var(--accent)}
.rssi-bar{height:4px;background:rgba(255,255,255,0.08);border-radius:2px;margin-top:6px;overflow:hidden}
.rssi-fill{height:100%;border-radius:2px;background:linear-gradient(90deg,var(--danger),#f59e0b,var(--success));transition:width .5s ease}
.bottom-nav{position:fixed;bottom:0;left:0;right:0;background:rgba(10,10,15,0.95);border-top:1px solid var(--border);backdrop-filter:blur(16px);display:flex;z-index:100;padding-bottom:env(safe-area-inset-bottom)}
.nav-btn{flex:1;padding:10px 4px 8px;background:none;border:none;color:var(--muted);cursor:pointer;font-size:9px;font-weight:500;text-transform:uppercase;letter-spacing:.5px;transition:color .2s;display:flex;flex-direction:column;align-items:center;gap:2px}
.nav-btn .nav-icon{font-size:20px;line-height:1}
.nav-btn.active{color:var(--accent)}
.nav-btn.active .nav-icon{filter:drop-shadow(0 0 6px var(--accent))}
.toast{position:fixed;top:16px;left:50%;transform:translateX(-50%) translateY(-80px);background:rgba(20,20,30,0.95);border:1px solid var(--border);border-radius:12px;padding:10px 18px;font-size:13px;font-weight:500;z-index:999;transition:transform .3s cubic-bezier(.34,1.56,.64,1);backdrop-filter:blur(16px);white-space:nowrap;pointer-events:none}
.toast.show{transform:translateX(-50%) translateY(0)}
.toast.success{border-color:rgba(16,185,129,.4);color:var(--success)}
.toast.error{border-color:rgba(239,68,68,.4);color:var(--danger)}
.llm-tabs{display:flex;gap:6px;margin-top:4px;flex-wrap:wrap}
.llm-tab{padding:6px 12px;border-radius:8px;border:1px solid rgba(255,255,255,0.1);background:rgba(255,255,255,0.04);font-size:11px;font-weight:600;cursor:pointer;color:var(--muted);transition:all .2s;text-transform:uppercase;letter-spacing:.8px}
.llm-tab.active{border-color:var(--accent);background:rgba(0,229,255,0.1);color:var(--accent)}
.sep{height:1px;background:var(--border);margin:14px 0}
.hint{font-size:10px;color:var(--muted);margin-top:4px;line-height:1.5}
</style>
</head>
<body>
<div class="hero">
<svg class="face" viewBox="0 0 80 80" xmlns="http://www.w3.org/2000/svg">
<defs>
<radialGradient id="fg" cx="50%" cy="50%" r="50%">
<stop offset="0%" stop-color="#1a1a2e"/>
<stop offset="100%" stop-color="#0d0d1a"/>
</radialGradient>
<filter id="glow">
<feGaussianBlur stdDeviation="2" result="blur"/>
<feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
</filter>
</defs>
<circle cx="40" cy="40" r="38" fill="url(#fg)" stroke="#00e5ff" stroke-width="1.5" opacity=".8"/>
<ellipse cx="26" cy="36" rx="9" ry="9" fill="#00e5ff" filter="url(#glow)" id="eL"/>
<ellipse cx="54" cy="36" rx="9" ry="9" fill="#00e5ff" filter="url(#glow)" id="eR"/>
<ellipse cx="26" cy="36" rx="4" ry="4.5" fill="#0a0a0f" id="pL"/>
<ellipse cx="54" cy="36" rx="4" ry="4.5" fill="#0a0a0f" id="pR"/>
<path d="M28 56 Q40 64 52 56" stroke="#00e5ff" stroke-width="2" fill="none" stroke-linecap="round" opacity=".7"/>
<circle cx="26" cy="23" r="2" fill="#00e5ff" opacity=".4"/>
<circle cx="54" cy="23" r="2" fill="#7c3aed" opacity=".4"/>
</svg>
<div class="hero-title">MELVIN</div>
<div class="hero-sub">Robot Configuration</div>
</div>

<div id="tab-wifi" class="tab-content active">
<div class="card">
<div class="card-title">WiFi сеть</div>
<label>Имя сети (SSID)</label>
<input type="text" id="wifi_ssid" placeholder="MyHomeWiFi" autocomplete="off">
<label>Пароль</label>
<div class="inp-wrap">
<input type="password" id="wifi_pass" placeholder="••••••••" autocomplete="off">
<button class="eye-btn" onclick="toggleVis('wifi_pass',this)">👁</button>
</div>
<div class="hint">После сохранения Мелвин перезагрузится и подключится к сети</div>
</div>
<div class="card">
<div class="card-title">Статус соединения</div>
<div class="wifi-status" id="wifi-status-block">
<div class="wifi-dot"></div>
<span id="wifi-status-text">Загрузка...</span>
</div>
<div class="rssi-bar"><div class="rssi-fill" id="rssi-fill" style="width:0%"></div></div>
<div class="hint" id="rssi-text" style="margin-top:6px">RSSI: —</div>
</div>
<button class="btn btn-primary" onclick="saveAll()">💾 Сохранить и перезагрузить</button>
<button class="btn btn-secondary btn-sm" style="width:100%;margin-top:6px" onclick="resetWifi()">🔄 Сброс WiFi</button>
</div>

<div id="tab-keys" class="tab-content">
<div class="card">
<div class="card-title">LLM Провайдер</div>
<div class="llm-tabs">
<div class="llm-tab active" id="llm-gemini" onclick="setLLM('gemini')">Gemini</div>
<div class="llm-tab" id="llm-groq" onclick="setLLM('groq')">Groq</div>
<div class="llm-tab" id="llm-openrouter" onclick="setLLM('openrouter')">OpenRouter</div>
<div class="llm-tab" id="llm-yandex" onclick="setLLM('yandex')">Yandex</div>
</div>
</div>
<div class="card">
<div class="card-title">API Ключи</div>
<label>Gemini ключи (через запятую)</label>
<div class="inp-wrap">
<input type="password" id="gemini_keys" placeholder="AIza..." autocomplete="off">
<button class="eye-btn" onclick="toggleVis('gemini_keys',this)">👁</button>
</div>
<div class="sep"></div>
<label>Groq ключи (через запятую)</label>
<div class="inp-wrap">
<input type="password" id="groq_keys" placeholder="gsk_..." autocomplete="off">
<button class="eye-btn" onclick="toggleVis('groq_keys',this)">👁</button>
</div>
<div class="sep"></div>
<label>OpenRouter ключи (через запятую)</label>
<div class="inp-wrap">
<input type="password" id="openrouter_keys" placeholder="sk-or-..." autocomplete="off">
<button class="eye-btn" onclick="toggleVis('openrouter_keys',this)">👁</button>
</div>
<div class="sep"></div>
<label>Yandex ключи (FolderID:ApiKey)</label>
<div class="inp-wrap">
<input type="password" id="yandex_keys" placeholder="FolderID:ApiKey" autocomplete="off">
<button class="eye-btn" onclick="toggleVis('yandex_keys',this)">👁</button>
</div>
<div class="sep"></div>
<label>API Прокси (например http://192.168.31.123:8080)</label>
<input type="text" id="api_proxy" placeholder="http://192.168.31.123:8080" autocomplete="off">
</div>
<div class="card">
<div class="card-title">TTS Синтез речи</div>
<label>Провайдер TTS</label>
<select id="tts_provider">
<option value="none">Без TTS (выкл.)</option>
<option value="yandex">Yandex SpeechKit (Поток LPCM)</option>
<option value="google_free">Google Translate (Поток MP3)</option>
</select>
<label>TTS API ключ</label>
<div class="inp-wrap">
<input type="password" id="tts_key" placeholder="Ключ для TTS..." autocomplete="off">
<button class="eye-btn" onclick="toggleVis('tts_key',this)">👁</button>
</div>
<label>Язык TTS</label>
<select id="tts_language">
    <option value="ru">Русский</option>
    <option value="en">English</option>
    <option value="de">Deutsch</option>
</select>
<label>Голос</label>
<input type="text" id="tts_voice" placeholder="ru-RU-Wavenet-B">
<div class="hint">Для Google: ru-RU-Wavenet-B, ru-RU-Wavenet-D и т.д.</div>
</div>
<button class="btn btn-primary" onclick="saveAll()">💾 Сохранить</button>
</div>

<div id="tab-pers" class="tab-content">
<div class="card">
<div class="card-title">Личность Мелвина</div>
<div class="personality-grid">
<div class="pers-card selected" id="pers-rick" onclick="setPersonality('rick')">
<div class="pers-icon">🥃</div>
<div class="pers-name">Рик</div>
<div class="pers-desc">Сарказм и цинизм</div>
</div>
<div class="pers-card" id="pers-calm" onclick="setPersonality('calm')">
<div class="pers-icon">😌</div>
<div class="pers-name">Спокойный</div>
<div class="pers-desc">Мягкий и вежливый</div>
</div>
<div class="pers-card" id="pers-podcast" onclick="setPersonality('podcast')">
<div class="pers-icon">🎙️</div>
<div class="pers-name">Подкаст</div>
<div class="pers-desc">Диктор новостей</div>
</div>
<div class="pers-card" id="pers-custom" onclick="setPersonality('custom')">
<div class="pers-icon">✍️</div>
<div class="pers-name">Свой</div>
<div class="pers-desc">Свой промпт</div>
</div>
</div>
</div>
<div class="card">
<div class="card-title">Настройки</div>
<label>Имя (слово-пробудитель)</label>
<input type="text" id="wake_word" placeholder="Мелвин">
<label>Системный промпт (для режима &quot;Свой&quot;)</label>
<textarea id="system_prompt" placeholder="Ты — Мелвин, умный робот. Отвечай кратко на русском..."></textarea>
<label>RSS лента новостей</label>
<input type="text" id="rss_url" placeholder="https://lenta.ru/rss/news">
</div>
<button class="btn btn-primary" onclick="saveAll()">💾 Сохранить</button>
</div>

<div id="tab-status" class="tab-content">
<div class="card">
<div class="card-title">Состояние</div>
<div style="text-align:center;margin-bottom:10px">
<div class="state-badge" id="state-badge">⏳ Загрузка...</div>
</div>
<div class="stat-grid">
<div class="stat-card"><div class="stat-val" id="s-heap">—</div><div class="stat-lbl">Heap (KB)</div></div>
<div class="stat-card"><div class="stat-val" id="s-psram">—</div><div class="stat-lbl">PSRAM (KB)</div></div>
<div class="stat-card"><div class="stat-val" id="s-uptime">—</div><div class="stat-lbl">Uptime (с)</div></div>
<div class="stat-card"><div class="stat-val" id="s-rssi">—</div><div class="stat-lbl">RSSI (dBm)</div></div>
<div class="stat-card"><div class="stat-val" id="s-sd">—</div><div class="stat-lbl">SD карта</div></div>
<div class="stat-card"><div class="stat-val" id="s-history">—</div><div class="stat-lbl">История</div></div>
</div>
</div>
<div class="card">
<div class="card-title">Управление</div>
<button class="btn btn-secondary" onclick="doRestart()">🔄 Перезагрузить Мелвина</button>
<button class="btn btn-danger" style="margin-top:8px" onclick="doDeleteHistory()">🗑️ Удалить историю</button>
</div>
</div>

<nav class="bottom-nav">
<button class="nav-btn active" id="nav-wifi" onclick="showTab('wifi')"><span class="nav-icon">📶</span>WiFi</button>
<button class="nav-btn" id="nav-keys" onclick="showTab('keys')"><span class="nav-icon">🔑</span>Ключи</button>
<button class="nav-btn" id="nav-pers" onclick="showTab('pers')"><span class="nav-icon">🤖</span>Личность</button>
<button class="nav-btn" id="nav-status" onclick="showTab('status')"><span class="nav-icon">📊</span>Статус</button>
</nav>

<div class="toast" id="toast"></div>

<script>
var _llm='gemini',_pers='rick',_statusTimer=null,_statusActive=false;

function showTab(t){
  ['wifi','keys','pers','status'].forEach(function(n){
    document.getElementById('tab-'+n).classList.remove('active');
    document.getElementById('nav-'+n).classList.remove('active');
  });
  document.getElementById('tab-'+t).classList.add('active');
  document.getElementById('nav-'+t).classList.add('active');
  if(t==='status'){startStatus();}else{stopStatus();}
}

function toggleVis(id,btn){
  var el=document.getElementById(id);
  if(el.type==='password'){el.type='text';btn.textContent='🙈';}
  else{el.type='password';btn.textContent='👁';}
}

function setLLM(v){
  _llm=v;
  ['gemini','groq','openrouter','yandex'].forEach(function(n){
    document.getElementById('llm-'+n).classList.remove('active');
  });
  document.getElementById('llm-'+v).classList.add('active');
}

function setPersonality(v){
  _pers=v;
  ['rick','calm','podcast','custom'].forEach(function(n){
    document.getElementById('pers-'+n).classList.remove('selected');
  });
  document.getElementById('pers-'+v).classList.add('selected');
}

function toast(msg,type){
  var t=document.getElementById('toast');
  t.textContent=msg;
  t.className='toast '+(type||'');
  t.classList.add('show');
  setTimeout(function(){t.classList.remove('show');},2800);
}

function gv(id){return document.getElementById(id).value.trim();}
function sv(id,v){document.getElementById(id).value=v||'';}

function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    sv('wifi_ssid',d.wifi_ssid);
    sv('gemini_keys',d.gemini_keys);
    sv('groq_keys',d.groq_keys);
    sv('openrouter_keys',d.openrouter_keys);
    sv('yandex_keys',d.yandex_keys);
    sv('api_proxy',d.api_proxy);
    sv('tts_key',d.tts_key);
    sv('tts_voice',d.tts_voice);
    sv('wake_word',d.wake_word);
    sv('rss_url',d.rss_url);
    sv('system_prompt',d.system_prompt);
    if(d.tts_provider)document.getElementById('tts_provider').value=d.tts_provider;
    if(d.llm_provider)setLLM(d.llm_provider);
    if(d.personality)setPersonality(d.personality);
    if(d.tts_language)document.getElementById('tts_language').value=d.tts_language;
  }).catch(function(){toast('Ошибка загрузки конфига','error');});
}

function saveAll(){
  if (gv('wifi_ssid').length < 2) {
    toast('⚠️ Введите имя WiFi сети', 'error'); return;
  }
  var ykeys = gv('yandex_keys');
  if (ykeys.length > 0 && !ykeys.includes(':')) {
    toast('⚠️ Yandex ключ: формат FolderID:ApiKey', 'error'); return;
  }
  var proxy = gv('api_proxy');
  if (proxy.length > 0 && !proxy.startsWith('http')) {
    toast('⚠️ Прокси должен начинаться с http://', 'error'); return;
  }

  var wifiPass = gv('wifi_pass');
  var data={
    wifi_ssid:gv('wifi_ssid'),
    wifi_pass:wifiPass.length > 0 ? wifiPass : '__KEEP__',
    gemini_keys:gv('gemini_keys'),
    groq_keys:gv('groq_keys'),
    openrouter_keys:gv('openrouter_keys'),
    yandex_keys:gv('yandex_keys'),
    api_proxy:gv('api_proxy'),
    tts_key:gv('tts_key'),
    tts_voice:gv('tts_voice'),
    tts_provider:document.getElementById('tts_provider').value,
    llm_provider:_llm,
    personality:_pers,
    wake_word:gv('wake_word'),
    system_prompt:gv('system_prompt'),
    rss_url:gv('rss_url'),
    tts_language:document.getElementById('tts_language').value
  };
  fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(data)})
  .then(function(r){
    if(r.ok){toast('Сохранено! Перезагружаюсь...','success');}
    else{toast('Ошибка сохранения','error');}
  }).catch(function(){toast('Нет связи с Мелвином','error');});
}

function resetWifi(){
  if(!confirm('Сбросить настройки WiFi?'))return;
  fetch('/api/reset-wifi',{method:'POST'}).then(function(){
    toast('WiFi сброшен','success');
  }).catch(function(){toast('Ошибка','error');});
}

function doRestart(){
  fetch('/api/restart',{method:'POST'}).then(function(){
    toast('Перезагрузка...','success');
  }).catch(function(){});
}

function doDeleteHistory(){
  if(!confirm('Удалить всю историю диалогов?'))return;
  fetch('/api/delete-history',{method:'POST'}).then(function(r){
    if(r.ok){toast('История удалена','success');}
    else{toast('Ошибка','error');}
  }).catch(function(){toast('Нет связи','error');});
}

function stateLabel(s) {
  var labels = {
    'IDLE':       'Ожидание',
    'RECORDING':  'Слушаю...',
    'THINKING':   'Думаю...',
    'SPEAKING':   'Говорю',
    'BOOT':       'Загрузка',
    'ERROR':      'Ошибка',
    'CONFIG_AP':  'Точка доступа',
    'CONNECTING': 'Подключение к WiFi'
  };
  return labels[s] || s;
}

function updateStatus(){
  fetch('/api/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('s-heap').textContent=d.heap?Math.round(d.heap/1024):'—';
    document.getElementById('s-psram').textContent=d.psram?Math.round(d.psram/1024):'—';
    document.getElementById('s-uptime').textContent=d.uptime||'—';
    document.getElementById('s-rssi').textContent=d.rssi||'—';
    document.getElementById('state-badge').textContent=stateEmoji(d.state)+' '+stateLabel(d.state||'?');
    var rssi=d.rssi||0;
    document.getElementById('rssi-fill').style.width=Math.max(0,Math.min(100,(rssi+90)*2))+'%';
    document.getElementById('rssi-text').textContent='RSSI: '+(rssi?rssi+' dBm':'—');
    document.getElementById('wifi-status-text').textContent=rssi?'Подключено ('+rssi+' dBm)':'Не в сети';
    
    if (d.sd_ok === false) {
      toast('⚠️ SD карта не найдена!', 'error');
    }
    document.getElementById('s-sd').textContent = d.sd_ok ? (Math.round(d.sd_used_kb) + '/' + Math.round(d.sd_total_kb) + ' KB') : 'Не найдена';
    document.getElementById('s-history').textContent = d.sd_ok ? (Math.round(d.history_kb) + ' KB') : '—';
  }).catch(function(){
    document.getElementById('state-badge').textContent='❓ Нет данных';
  });
}

function stateEmoji(s){
  var m={'IDLE':'💤','RECORDING':'🎙','THINKING':'🧠','SPEAKING':'🔊','BOOT':'🚀','ERROR':'❌','CONFIG_AP':'📡','CONNECTING':'🔗'};
  return m[s]||'❓';
}

function startStatus(){
  if(_statusActive)return;
  _statusActive=true;
  updateStatus();
  _statusTimer=setInterval(updateStatus,3000);
}

function stopStatus(){
  _statusActive=false;
  if(_statusTimer){clearInterval(_statusTimer);_statusTimer=null;}
}

(function blink(){
  function doB(){
    var eL=document.getElementById('pL'),eR=document.getElementById('pR');
    if(!eL)return;
    eL.setAttribute('ry','0.5');eR.setAttribute('ry','0.5');
    setTimeout(function(){eL.setAttribute('ry','4.5');eR.setAttribute('ry','4.5');},120);
  }
  setInterval(doB,Math.random()*2000+2000);
  doB();
})();

loadConfig();
updateStatus(); // обновить RSSI сразу при загрузке страницы
</script>
</body>
</html>)rawliteral";
#endif
