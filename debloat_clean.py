import subprocess

adb = r"C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"

bloatware = [
    # MIUI Реклама, телеметрия и трекинг
    "com.miui.analytics",
    "com.miui.msa.global",
    "com.miui.daemon",
    "com.miui.cleanmaster",
    "com.miui.hybrid",
    "com.miui.hybrid.accessory",
    "com.miui.bugreport",
    "com.miui.klo.bugreport",
    "com.xiaomi.discover",
    "com.xiaomi.payment",
    "com.mipay.wallet.id",
    "com.mipay.wallet.in",
    "com.miui.yellowpage",
    "com.miui.cloudservice",
    "com.miui.cloudservice.sysbase",
    "com.android.browser",
    "com.miui.compass",
    "com.xiaomi.scanner",

    # Google фоновые тяжелые службы
    "com.google.android.googlequicksearchbox",
    "com.google.android.youtube",
    "com.google.android.apps.docs",
    "com.google.android.apps.photos",
    "com.google.android.apps.maps",
    "com.google.android.apps.tachyon",
    "com.google.android.music",
    "com.google.android.videos",
    "com.google.android.talk",
    "com.google.android.marvin.talkback",
    "com.google.android.feedback"
]

print("=== ФИНАЛЬНАЯ ПРОВЕРКА И ДООЧИСТКА БЛОАТВАРА ===")
success_count = 0
for pkg in bloatware:
    cmd = [adb, "shell", f"pm uninstall -k --user 0 {pkg}"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    out = res.stdout.strip()
    if "Success" in out:
        print(f" [УДАЛЕНО] {pkg}")
        success_count += 1
    elif "not installed" in out.lower():
        print(f" [УЖЕ БЫЛ УДАЛЕН] {pkg}")
        success_count += 1
    else:
        print(f" [ПРОПУСК / {out}] {pkg}")

print(f"\nИтог: {success_count} из {len(bloatware)} вычищены.")

# Убиваем dex2oat и фоновые кэши
subprocess.run([adb, "shell", "pkill -f dex2oat"], capture_output=True)
subprocess.run([adb, "shell", "am kill-all"], capture_output=True)

res = subprocess.run([adb, "shell", "cat /proc/meminfo"], capture_output=True, text=True)
for line in res.stdout.splitlines():
    if any(k in line for k in ["MemTotal", "MemFree", "MemAvailable", "Cached", "SwapTotal", "SwapFree"]):
        print(line)
