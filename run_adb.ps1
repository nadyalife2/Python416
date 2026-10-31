param([string]$cmd)
$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb shell "$cmd"
