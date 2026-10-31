$adb = "C:\Program Files (x86)\OnePlus USB Drivers\Android\adb.exe"
& $adb shell 'PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys /bin/busybox sh -c "PATH=/bin:/sbin:/usr/bin:/usr/sbin; pip install flask requests --break-system-packages"'
& $adb shell 'PROOT_TMP_DIR=/data/local/tmp /data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys /bin/busybox sh -c "PATH=/bin:/sbin:/usr/bin:/usr/sbin; python3 -c \"import flask, sqlite3; print(flask.__version__)\""'
