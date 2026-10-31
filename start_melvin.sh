#!/bin/sh
export PROOT_TMP_DIR=/data/local/tmp

echo "=== ОСТАНОВКА СТАРЫХ ПРОЦЕССОВ ==="
pkill -9 llama-server 2>/dev/null
pkill -9 python3 2>/dev/null
sleep 1

/data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys -b /sdcard/melvin:/melvin /bin/busybox sh -c '
    export PATH=/bin:/sbin:/usr/bin:/usr/sbin
    echo "=== ЗАПУСК ЛОКАЛЬНОГО ИИ-АГЕНТА МЕЛВИН (FAST QWEN2.5) ==="
    echo "SoC: MediaTek Helio X20 MT6797"
    echo "Модель: Qwen2.5-0.5B-Instruct (Q4_K_M) - High-Speed Conversational"
    echo "Память: SQLite FTS5 RAG (/melvin/data/agent_memory.db)"

    export OPENBLAS_NUM_THREADS=4
    export OMP_NUM_THREADS=4
    
    llama-server -m /melvin/models/qwen2.5-0.5b-instruct-q4_k_m.gguf \
                 -c 1024 \
                 -t 4 \
                 -np 1 \
                 --host 0.0.0.0 \
                 --port 8080 > /melvin/data/llama_server.log 2>&1 &

    echo "Ожидание готовности LLM-сервера..."
    for i in $(seq 1 30); do
        if curl -s http://127.0.0.1:8080/health | grep -q "ok"; then
            echo "LLM-сервер успешно запущен на порту 8080!"
            break
        fi
        sleep 1
    done

    echo "Запуск оркестратора на порту 5000..."
    python3 /melvin/agent_orchestrator.py
'
