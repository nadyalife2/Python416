for i in $(seq 1 50); do
    res=$(pm disable-user com.google.android.gms/com.google.android.location.internal.GoogleLocationManagerService 2>&1)
    echo "Attempt $i: $res"
    if echo "$res" | grep -qiE "new state|disabled"; then
        echo "=== FIX APPLIED SUCCESSFULLY ==="
        break
    fi
    sleep 0.4
done
