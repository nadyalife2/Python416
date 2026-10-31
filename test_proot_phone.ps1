$url = "https://github.com/proot-me/proot/releases/download/v5.3.0/proot-v5.3.0-aarch64-static"
$dest = "D:\esp32\proot-aarch64"
& curl.exe -L -k -o $dest $url

$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb push $dest /data/local/tmp/proot
& $adb shell "chmod 755 /data/local/tmp/proot"
& $adb shell "/data/local/tmp/proot --version"
