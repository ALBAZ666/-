#!/system/bin/sh
export LD_LIBRARY_PATH=/data/local/tmp/asr
export SENSEVOICE_MODEL=/data/local/tmp/asr/model.int8.onnx
export SENSEVOICE_TOKENS=/data/local/tmp/asr/tokens.txt
cd /data/local/tmp/asr
nohup /data/local/tmp/asr/asr_server_arm64 > /data/local/tmp/asr/asr_server.log 2>&1 &
