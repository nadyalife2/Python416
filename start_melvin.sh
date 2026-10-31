#!/system/bin/sh
# Melvin Turbo Rick On-Device AI Stack (Helio X20 MT6797 • Nikel)
# Pure ext4 Native Execution + Bionic Host + PRoot Orchestrator

export PROOT_TMP_DIR=/data/local/tmp

echo "=== ОСТАНОВКА СТАРЫХ ПРОЦЕССОВ ==="
pkill -9 llama-server 2>/dev/null
pkill -9 python3 2>/dev/null
sleep 1

echo "=== ЗАПУСК NATIVE LLAMA-SERVER (EXT4 • NO-MMAP • NO-PROOT) ==="
echo "Hardware: MT6797 Tri-Cluster (Cortex-A53 / A72)"
echo "Weights: Qwen2.5-0.5B-Instruct-Q4_0.gguf (Native NEON Interleaved)"

export OPENBLAS_NUM_THREADS=1
export OMP_NUM_THREADS=4
export OMP_WAIT_POLICY=PASSIVE

# Запуск чистого нативного llama-server вне PRoot (без ptrace-оверхеда)
/data/local/tmp/melvin-bin/llama-server \
    -m /data/local/tmp/models/qwen2.5-0.5b-instruct-q4_0.gguf \
    -c 512 \
    -b 512 \
    -ub 256 \
    -t 4 \
    -tb 4 \
    -np 1 \
    -ctk f16 \
    -ctv f16 \
    -lm none \
    --host 127.0.0.1 \
    --port 8080 > /data/local/tmp/llama_server.log 2>&1 &

echo "Ожидание готовности LLM-сервера (порт 8080)..."
for i in $(seq 1 30); do
    if grep -q "listening on http" /data/local/tmp/llama_server.log 2>/dev/null; then
        echo "LLM-сервер успешно слушает на 127.0.0.1:8080!"
        break
    fi
    sleep 1
done

echo "=== НАСТРОЙКА СЕТИ И DNS ДЛЯ ALPINE PROOT ==="
DNS_IP=$(getprop net.dns1)
if [ -n "$DNS_IP" ]; then
    echo "nameserver $DNS_IP" > /data/local/tmp/alpine/etc/resolv.conf
fi
echo "nameserver 8.8.8.8" >> /data/local/tmp/alpine/etc/resolv.conf

echo "=== ЗАПУСК ОРКЕСТРАТОРА (PYTHON3 • FLASK • FTS5 RAG) ==="
/data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys -b /sdcard/melvin:/melvin /bin/busybox sh -c '
    export PATH=/bin:/sbin:/usr/bin:/usr/sbin
    python3 /melvin/agent_orchestrator.py
'
