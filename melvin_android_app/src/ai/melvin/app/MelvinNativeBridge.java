package ai.melvin.app;

import android.webkit.JavascriptInterface;

public class MelvinNativeBridge {
    private final MainActivity activity;

    public MelvinNativeBridge(MainActivity activity) {
        this.activity = activity;
    }

    @JavascriptInterface
    public boolean isNative() {
        return true;
    }

    @JavascriptInterface
    public void startVoice() {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.startVoiceRecognition();
            }
        });
    }

    @JavascriptInterface
    public void speak(final String text) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.speakText(text);
            }
        });
    }

    @JavascriptInterface
    public void setPitch(final float pitch) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.setTtsPitch(pitch);
            }
        });
    }

    @JavascriptInterface
    public void setRate(final float rate) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.setTtsRate(rate);
            }
        });
    }

    @JavascriptInterface
    public void vibrate(final int milliseconds) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.triggerVibrate(milliseconds);
            }
        });
    }

    @JavascriptInterface
    public void keepScreenOn(final boolean keepOn) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.setScreenWake(keepOn);
            }
        });
    }

    @JavascriptInterface
    public void refresh() {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                activity.reloadMelvin();
            }
        });
    }
}
