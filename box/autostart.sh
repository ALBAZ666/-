#!/system/bin/sh
# 小卡盒子本地服务自启脚本：Qwen + ASR
LOG=/data/local/tmp/autostart.log

# Qwen llama-server
if [ -z "$(pgrep -f llama-server)" ]; then
  cd /data/local/tmp/xiaoka
  export LD_LIBRARY_PATH=/data/local/tmp/llama-b10514
  export GGML_BACKEND_PATH=/data/local/tmp/llama-b10514/libggml-cpu-android_armv8.2_1.so
  nohup setsid ./llama-server -m Qwen3-0.6B-Q4_K_M.gguf --host 0.0.0.0 --port 8080 > llama.log 2>&1 < /dev/null &
  echo "$(date) llama started" >> $LOG
fi

# ASR
if [ -z "$(pgrep -f asr_server)" ]; then
  cd /data/local/tmp/asr
  export LD_LIBRARY_PATH=/data/local/tmp/asr
  export SENSEVOICE_MODEL=/data/local/tmp/asr/model.int8.onnx
  export SENSEVOICE_TOKENS=/data/local/tmp/asr/tokens.txt
  nohup setsid /data/local/tmp/asr/asr_server_arm64 > /data/local/tmp/asr/asr_server.log 2>&1 < /dev/null &
  echo "$(date) asr started" >> $LOG
fi
