$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb shell "chmod 755 /data/local/tmp/alpine/bin/busybox"
& $adb shell "PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys /bin/busybox sh -c 'PATH=/bin:/sbin:/usr/bin:/usr/sbin; uname -a; cat /etc/os-release'"
