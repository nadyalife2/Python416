$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"

Write-Host "=== 1. ALL INSTALLED PACKAGES ==="
$allPkgs = & $adb shell "pm list packages"
$pkgList = $allPkgs | ForEach-Object { $_.Trim().Replace("package:", "") } | Where-Object { $_ -ne "" }

Write-Host "Total packages installed: $($pkgList.Count)"

Write-Host "`n=== 2. ROOT MANAGERS / UTILS ==="
$rootPkgs = $pkgList | Where-Object { $_ -match "root|magisk|supersu|superuser|king" }
$rootPkgs | ForEach-Object { Write-Host " - $_" }

Write-Host "`n=== 3. XIAOMI / MIUI BLOATWARE ==="
$miuiPkgs = $pkgList | Where-Object { $_ -match "analytics|msa|cloudservice|daemon|hybrid|discover|bugreport|feedback|game|yellowpage|payment|mibi|mipay|micredit|compass|scanner|browser|cleanmaster" }
$miuiPkgs | ForEach-Object { Write-Host " - $_" }

Write-Host "`n=== 4. GOOGLE PACKAGES ==="
$googlePkgs = $pkgList | Where-Object { $_ -match "google" }
$googlePkgs | ForEach-Object { Write-Host " - $_" }

Write-Host "`n=== 5. CURRENT MEMORY STATE ==="
& $adb shell "cat /proc/meminfo" | Select-String -Pattern "MemTotal|MemFree|MemAvailable|Buffers|Cached|SwapTotal|SwapFree"

Write-Host "`n=== 6. TOP 15 CPU / MEM PROCESSES ==="
& $adb shell "top -n 1 -m 15 -s cpu"

Write-Host "`n=== 7. CHECK RUNNING SERVICES ==="
$services = & $adb shell "dumpsys activity services" | Select-String -Pattern "ServiceRecord{"
Write-Host "Total active background services: $($services.Count)"
