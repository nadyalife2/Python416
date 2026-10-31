$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
Write-Host "Installing llama-server, python3, sqlite in Alpine on phone..."
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys /bin/busybox sh -c 'PATH=/bin:/sbin:/usr/bin:/usr/sbin; apk add --no-cache llama-server llama.cpp python3 py3-pip sqlite curl'"
