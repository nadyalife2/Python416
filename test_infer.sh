export PROOT_TMP_DIR=/data/local/tmp
/data/local/tmp/proot -r /data/local/tmp/alpine -0 -b /dev -b /proc -b /sys -b /sdcard/melvin:/melvin /bin/busybox sh -c "export PATH=/bin:/sbin:/usr/bin:/usr/sbin; llama-cli -m /melvin/models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_M.gguf -p 'Ты Рик Санчез. Ответь кратко: кто ты?' -n 32 -t 2 --simple-io"
