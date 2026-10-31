package ai.melvin.app;

import android.Manifest;
import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.os.Vibrator;
import android.speech.RecognizerIntent;
import android.speech.tts.TextToSpeech;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.webkit.PermissionRequest;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceError;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Locale;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

public class MainActivity extends Activity implements TextToSpeech.OnInitListener {

    private static final String MELVIN_URL = "http://127.0.0.1:5000/";
    private static final String HEALTH_URL = "http://127.0.0.1:5000/health";
    private static final int REQ_CODE_SPEECH = 1001;
    private static final int REQ_CODE_PERMS = 2002;

    private WebView webView;
    private LinearLayout layoutOffline;
    private ImageView imgStatusDot;
    private TextView txtHeaderTitle;
    private TextView txtOfflineDesc;
    private ProgressBar progressConnect;
    private Button btnWakeLock;
    private Button btnVoice;
    private Button btnReload;
    private Button btnRetry;
    private Button btnStartDaemon;

    private boolean isScreenWakeActive = false;
    private boolean isConnected = false;
    private boolean hasInitialCheckRun = false;
    private PowerManager.WakeLock wakeLock;
    private TextToSpeech textToSpeech;
    private ScheduledExecutorService healthScheduler;
    private final Handler mainHandler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // Dark system status bar
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
            Window window = getWindow();
            window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
            window.setStatusBarColor(Color.parseColor("#131924"));
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP_MR1) {
                window.setNavigationBarColor(Color.parseColor("#0a0e14"));
            }
        }

        setContentView(R.layout.activity_main);

        initViews();
        setupWakeLock();
        setupWebView();
        setupTTS();
        checkAndRequestPermissions();
        checkHealthImmediate();
        startHealthChecker();
    }

    private void initViews() {
        webView = (WebView) findViewById(R.id.webView);
        layoutOffline = (LinearLayout) findViewById(R.id.layoutOffline);
        imgStatusDot = (ImageView) findViewById(R.id.imgStatusDot);
        txtHeaderTitle = (TextView) findViewById(R.id.txtHeaderTitle);
        txtOfflineDesc = (TextView) findViewById(R.id.txtOfflineDesc);
        progressConnect = (ProgressBar) findViewById(R.id.progressConnect);
        btnWakeLock = (Button) findViewById(R.id.btnWakeLock);
        btnVoice = (Button) findViewById(R.id.btnVoice);
        btnReload = (Button) findViewById(R.id.btnReload);
        btnRetry = (Button) findViewById(R.id.btnRetry);
        btnStartDaemon = (Button) findViewById(R.id.btnStartDaemon);

        btnWakeLock.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                toggleScreenWake();
            }
        });

        btnVoice.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startVoiceRecognition();
            }
        });

        btnReload.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                reloadMelvin();
            }
        });

        btnRetry.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                checkHealthImmediate();
            }
        });

        btnStartDaemon.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                tryStartDaemon();
            }
        });
    }

    private void setupWakeLock() {
        PowerManager pm = (PowerManager) getSystemService(Context.POWER_SERVICE);
        if (pm != null) {
            wakeLock = pm.newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "MelvinAI:KeepAliveLock");
            wakeLock.setReferenceCounted(false);
        }
    }

    public void setScreenWake(boolean enable) {
        isScreenWakeActive = enable;
        if (enable) {
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            if (wakeLock != null && !wakeLock.isHeld()) {
                wakeLock.acquire();
            }
            btnWakeLock.setTextColor(Color.parseColor("#00ff9d"));
            btnWakeLock.setText("☀️ ON");
        } else {
            getWindow().clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            if (wakeLock != null && wakeLock.isHeld()) {
                wakeLock.release();
            }
            btnWakeLock.setTextColor(Color.parseColor("#8b949e"));
            btnWakeLock.setText("☀️");
        }
    }

    private void toggleScreenWake() {
        setScreenWake(!isScreenWakeActive);
        Toast.makeText(this, isScreenWakeActive ? "Экран и процессор всегда активны" : "Энергосбережение включено", Toast.LENGTH_SHORT).show();
    }

    private void setupWebView() {
        WebSettings ws = webView.getSettings();
        ws.setJavaScriptEnabled(true);
        ws.setDomStorageEnabled(true);
        ws.setDatabaseEnabled(true);
        ws.setAllowFileAccess(true);
        ws.setLoadWithOverviewMode(true);
        ws.setUseWideViewPort(true);
        ws.setSupportZoom(false);
        ws.setMediaPlaybackRequiresUserGesture(false);

        // Native JS Bridge
        webView.addJavascriptInterface(new MelvinNativeBridge(this), "MelvinNativeBridge");

        webView.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onPermissionRequest(final PermissionRequest request) {
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        request.grant(request.getResources());
                    }
                });
            }
        });

        webView.setWebViewClient(new WebViewClient() {
            @Override
            public void onReceivedError(WebView view, WebResourceRequest request, WebResourceError error) {
                super.onReceivedError(view, request, error);
                if (request != null && request.isForMainFrame()) {
                    showOfflineUI("Ошибка соединения: 127.0.0.1:5000 недоступен");
                }
            }

            @SuppressWarnings("deprecation")
            @Override
            public void onReceivedError(WebView view, int errorCode, String description, String failingUrl) {
                super.onReceivedError(view, errorCode, description, failingUrl);
                if (failingUrl != null && failingUrl.contains("5000")) {
                    showOfflineUI("Ошибка соединения: " + description);
                }
            }

            @Override
            public void onPageFinished(WebView view, String url) {
                super.onPageFinished(view, url);
                if (url != null && url.contains("5000")) {
                    showOnlineUI();
                }
            }
        });
    }

    private void setupTTS() {
        textToSpeech = new TextToSpeech(this, this, "com.google.android.tts");
    }

    @Override
    public void onInit(int status) {
        if (status == TextToSpeech.SUCCESS && textToSpeech != null) {
            textToSpeech.setLanguage(new Locale("ru", "RU"));
            textToSpeech.setPitch(0.82f); // Deep, solid masculine baritone
            textToSpeech.setSpeechRate(1.05f); // Crisp natural tempo

            if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.LOLLIPOP) {
                try {
                    java.util.Set<android.speech.tts.Voice> voices = textToSpeech.getVoices();
                    if (voices != null) {
                        for (android.speech.tts.Voice v : voices) {
                            if (v != null && v.getLocale() != null && "ru".equalsIgnoreCase(v.getLocale().getLanguage())) {
                                String name = v.getName().toLowerCase();
                                if (name.contains("male") || name.contains("ruf") || name.contains("dfc") || name.contains("man") || name.contains("ruc")) {
                                    textToSpeech.setVoice(v);
                                    break;
                                }
                            }
                        }
                    }
                } catch (Exception ignored) {}
            }
        }
    }

    public void setTtsPitch(float pitch) {
        if (textToSpeech != null) {
            textToSpeech.setPitch(pitch);
        }
    }

    public void setTtsRate(float rate) {
        if (textToSpeech != null) {
            textToSpeech.setSpeechRate(rate);
        }
    }

    public void speakText(String text) {
        if (textToSpeech != null) {
            textToSpeech.speak(text, TextToSpeech.QUEUE_FLUSH, null, "melvin_speech");
        }
    }

    public void triggerVibrate(int ms) {
        Vibrator v = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        if (v != null && v.hasVibrator()) {
            v.vibrate(ms);
        }
    }

    public void startVoiceRecognition() {
        try {
            Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL, RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);
            intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE, "ru-RU");
            intent.putExtra(RecognizerIntent.EXTRA_PROMPT, getString(R.string.voice_prompt));
            startActivityForResult(intent, REQ_CODE_SPEECH);
        } catch (Exception e) {
            Toast.makeText(this, "Голосовой ввод недоступен", Toast.LENGTH_SHORT).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_CODE_SPEECH && resultCode == RESULT_OK && data != null) {
            ArrayList<String> matches = data.getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS);
            if (matches != null && !matches.isEmpty()) {
                String spokenText = matches.get(0);
                injectVoiceResult(spokenText);
            }
        }
    }

    private void injectVoiceResult(String text) {
        String safeText = text.replace("\\", "\\\\").replace("'", "\\'").replace("\"", "\\\"").replace("\n", " ");
        String js = "(function() {" +
                "  if (typeof window.onNativeVoiceInput === 'function') {" +
                "    window.onNativeVoiceInput('" + safeText + "');" +
                "  } else {" +
                "    var input = document.getElementById('prompt-input');" +
                "    if (input) {" +
                "      input.value = '" + safeText + "';" +
                "      if (typeof send === 'function') send();" +
                "    }" +
                "  }" +
                "})();";
        webView.evaluateJavascript(js, null);
    }

    public void reloadMelvin() {
        if (isConnected && webView.getUrl() != null) {
            webView.reload();
        } else {
            checkHealthImmediate();
        }
    }

    private void checkAndRequestPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            String[] perms = new String[]{
                    Manifest.permission.RECORD_AUDIO,
                    Manifest.permission.MODIFY_AUDIO_SETTINGS,
                    Manifest.permission.WAKE_LOCK
            };
            boolean needRequest = false;
            for (String p : perms) {
                if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) {
                    needRequest = true;
                    break;
                }
            }
            if (needRequest) {
                requestPermissions(perms, REQ_CODE_PERMS);
            }
        }
    }

    private void startHealthChecker() {
        healthScheduler = Executors.newSingleThreadScheduledExecutor();
        healthScheduler.scheduleWithFixedDelay(new Runnable() {
            @Override
            public void run() {
                checkHealthStatus();
            }
        }, 1, 3, TimeUnit.SECONDS);
    }

    private void checkHealthImmediate() {
        Executors.newSingleThreadExecutor().execute(new Runnable() {
            @Override
            public void run() {
                checkHealthStatus();
            }
        });
    }

    private void checkHealthStatus() {
        boolean ok = false;
        try {
            URL url = new URL(HEALTH_URL);
            HttpURLConnection conn = (HttpURLConnection) url.openConnection();
            conn.setConnectTimeout(1500);
            conn.setReadTimeout(1500);
            conn.setRequestMethod("GET");
            int code = conn.getResponseCode();
            if (code == 200) {
                ok = true;
            }
            conn.disconnect();
        } catch (Exception ignored) {
            ok = false;
        }

        final boolean healthOk = ok;
        mainHandler.post(new Runnable() {
            @Override
            public void run() {
                if (healthOk) {
                    if (!isConnected) {
                        isConnected = true;
                        showOnlineUI();
                        webView.loadUrl(MELVIN_URL);
                    }
                } else {
                    if (isConnected || !hasInitialCheckRun) {
                        isConnected = false;
                        showOfflineUI("Связь с ядром Мелвина не установлена (127.0.0.1:5000)");
                    }
                }
                hasInitialCheckRun = true;
            }
        });
    }

    private void showOnlineUI() {
        layoutOffline.setVisibility(View.GONE);
        webView.setVisibility(View.VISIBLE);
        imgStatusDot.setImageResource(R.drawable.dot_online);
        txtHeaderTitle.setText("MELVIN AI // ONLINE");
        txtHeaderTitle.setTextColor(Color.parseColor("#00ff9d"));
    }

    private void showOfflineUI(String reason) {
        layoutOffline.setVisibility(View.VISIBLE);
        imgStatusDot.setImageResource(R.drawable.dot_offline);
        txtHeaderTitle.setText("MELVIN AI // OFFLINE");
        txtHeaderTitle.setTextColor(Color.parseColor("#f85149"));
        txtOfflineDesc.setText(reason);
    }

    private void tryStartDaemon() {
        Toast.makeText(this, "Попытка запуска демона...", Toast.LENGTH_SHORT).show();
        Executors.newSingleThreadExecutor().execute(new Runnable() {
            @Override
            public void run() {
                try {
                    // Try executing proot start script if local shell permits
                    Process p = Runtime.getRuntime().exec(new String[]{
                            "sh", "-c", "nohup /data/local/tmp/start_melvin.sh > /sdcard/melvin/start.log 2>&1 &"
                    });
                    p.waitFor();
                } catch (Exception e) {
                    // Ignored
                }
                mainHandler.postDelayed(new Runnable() {
                    @Override
                    public void run() {
                        checkHealthImmediate();
                    }
                }, 2000);
            }
        });
    }

    @Override
    public void onBackPressed() {
        if (webView.canGoBack()) {
            webView.goBack();
        } else {
            super.onBackPressed();
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (healthScheduler != null) {
            healthScheduler.shutdownNow();
        }
        if (textToSpeech != null) {
            textToSpeech.stop();
            textToSpeech.shutdown();
        }
        if (wakeLock != null && wakeLock.isHeld()) {
            wakeLock.release();
        }
    }
}
