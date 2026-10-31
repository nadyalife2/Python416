$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"

Write-Host "Unpacking on Windows..."
New-Item -ItemType Directory -Path "D:\esp32\alpine" -Force | Out-Null
& tar.exe -xzf "D:\esp32\alpine-minirootfs-aarch64.tar.gz" -C "D:\esp32\alpine"

Write-Host "Creating uncompressed tar..."
& tar.exe -cf "D:\esp32\alpine-raw.tar" -C "D:\esp32\alpine" .

Write-Host "Pushing uncompressed tar to phone..."
& $adb push "D:\esp32\alpine-raw.tar" /data/local/tmp/alpine-raw.tar

Write-Host "Extracting on phone..."
& $adb shell "mkdir -p /data/local/tmp/alpine && toybox tar -xf /data/local/tmp/alpine-raw.tar -C /data/local/tmp/alpine"

Write-Host "Testing proot inside Alpine..."
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -b /dev -b /proc -b /sys -w / /bin/sh -c 'uname -a; cat /etc/os-release'"
