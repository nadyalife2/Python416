for i in $(seq 1 40); do
    res=$(pm clear com.google.android.gms 2>&1)
    echo "Attempt $i: $res"
    if echo "$res" | grep -qi "success"; then
        echo "=== CLEARED GMS DATA ==="
        break
    fi
    sleep 0.4
done
