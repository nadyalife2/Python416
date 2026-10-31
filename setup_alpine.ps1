$url = "https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/aarch64/alpine-minirootfs-3.20.0-aarch64.tar.gz"
$dest = "D:\esp32\alpine-minirootfs-aarch64.tar.gz"
Write-Host "Downloading Alpine Linux aarch64 minirootfs (3.5 MB)..."
& curl.exe -L -k -o $dest $url

$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
Write-Host "Creating /data/local/tmp/alpine..."
& $adb shell "mkdir -p /data/local/tmp/alpine"
& $adb push $dest /data/local/tmp/alpine.tar.gz

Write-Host "Unpacking minirootfs on phone..."
& $adb shell "cd /data/local/tmp/alpine && /system/bin/tar -xzf /data/local/tmp/alpine.tar.gz"

Write-Host "Testing Alpine chroot with proot..."
& $adb shell "/data/local/tmp/proot -r /data/local/tmp/alpine -b /dev -b /proc -b /sys /bin/sh -c 'uname -a; cat /etc/os-release'"
