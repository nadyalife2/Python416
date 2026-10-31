$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb shell "mkdir -p /sdcard/melvin/models /sdcard/melvin/data"
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys -b /sdcard/melvin:/melvin /bin/busybox sh -c 'PATH=/bin:/sbin:/usr/bin:/usr/sbin; apk add --no-cache py3-flask; ls -la /melvin'"
