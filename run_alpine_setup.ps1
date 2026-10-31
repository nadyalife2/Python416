$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb push "D:\esp32\alpine_setup.sh" /data/local/tmp/alpine/root/setup.sh
& $adb shell "chmod 755 /data/local/tmp/alpine/root/setup.sh"
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys /bin/busybox sh /root/setup.sh"
