$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb shell "chmod -R 755 /data/local/tmp/alpine/bin /data/local/tmp/alpine/sbin /data/local/tmp/alpine/lib /data/local/tmp/alpine/usr"
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys /bin/sh -c 'uname -a; cat /etc/os-release'"
