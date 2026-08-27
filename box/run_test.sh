export LD_LIBRARY_PATH=/data/local/tmp/llama-b10514
export GGML_BACKEND_PATH=/data/local/tmp/llama-b10514/libggml-cpu-android_armv8.2_1.so
export GGML_NUMA=0
cd /data/local/tmp/xiaoka
(echo "小卡小卡，下一首"; sleep 1; echo "/exit") | /data/local/tmp/llama-b10514/llama-cli -m /data/local/tmp/xiaoka/Qwen3-0.6B-Q4_K_M.gguf -n 30 -c 256 2>&1
