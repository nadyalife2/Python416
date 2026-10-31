#!/system/bin/sh
export LD_LIBRARY_PATH=/data/local/tmp/alpine/usr/lib/llama.cpp:/data/local/tmp/alpine/usr/lib:/data/local/tmp/alpine/lib
export GGML_BACKEND_PATH=/data/local/tmp/alpine/usr/lib/libggml-cpu-armv8.0_1.so
exec /data/local/tmp/alpine/lib/ld-musl-aarch64.so.1 --library-path /data/local/tmp/alpine/usr/lib/llama.cpp:/data/local/tmp/alpine/usr/lib:/data/local/tmp/alpine/lib /data/local/tmp/alpine/usr/bin/llama-server "$@"
