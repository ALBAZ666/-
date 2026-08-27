#!/system/bin/sh
export LD_LIBRARY_PATH=/data/local/tmp/llama-b10514
export GGML_BACKEND_PATH=/data/local/tmp/llama-b10514/libggml-cpu-android_armv8.2_1.so
cd /data/local/tmp/xiaoka
nohup ./llama-server -m Qwen3-0.6B-Q4_K_M.gguf --host 0.0.0.0 --port 8080 > /data/local/tmp/xiaoka/llama.log 2>&1 &
