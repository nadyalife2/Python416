$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"

$bloatware = @(
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

    # Google фоновые тяжелые службы (освобождаем 300+ МБ RAM)
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
)

Write-Host "=== ОЧИСТКА REDMI NOTE 4 ОТ ФОНОВОГО МУСОРА ==="
$successCount = 0
foreach ($pkg in $bloatware) {
    Write-Host -NoNewline "Удаление $pkg ... "
    $res = & $adb shell "pm uninstall -k --user 0 $pkg"
    if ($res -match "Success") {
        Write-Host " [OK]" -ForegroundColor Green
        $successCount++
    } else {
        Write-Host " [$($res.Trim())]" -ForegroundColor Yellow
    }
}

Write-Host "`nУспешно отключено пакетов: $successCount из $($bloatware.Count)"

Write-Host "`nУбиваем зависшие фоновые задачи (включая dex2oat)..."
& $adb shell "pkill -f dex2oat"
& $adb shell "am kill-all"

Write-Host "`n=== СОСТОЯНИЕ ОПЕРАТИВНОЙ ПАМЯТИ ПОСЛЕ ОЧИСТКИ ==="
& $adb shell "cat /proc/meminfo" | Select-String -Pattern "MemTotal|MemFree|MemAvailable|Cached"
