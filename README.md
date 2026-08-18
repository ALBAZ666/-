# 小卡 Agent

小卡是一个本地桌面语音智能体原型。打开网页后，右下角会出现麦克风按钮；点击后会持续监听，听到“小卡小卡”后进入指令模式。

## 启动

```bat
cd /d 项目文件夹
npm start
```

然后打开：

```text
http://localhost:8123
```

## 当前可用指令

- 小卡小卡，下一首
- 小卡小卡，上一首
- 小卡小卡，暂停
- 小卡小卡，继续播放
- 小卡小卡，音量大一点
- 小卡小卡，音量小一点
- 小卡小卡，静音

## 关于 Qwen

启动脚本默认开启本地 Qwen3-0.6B 意图理解。Qwen 会在后台常驻，只加载一次；如果模型进程启动失败，Agent 会自动回退到规则识别。

```bat
set XIAOKA_USE_QWEN=1
set XIAOKA_PYTHON=C:\Users\Administrator\AppData\Local\Programs\Python\Python313\python.exe
set XIAOKA_QWEN_MODEL=E:\AI\Models\Qwen3-0.6B
npm start
```

分享包不包含约 1.5GB 的 Qwen 模型文件。使用 Qwen 时，请将模型目录放在项目同级的 `Models\Qwen3-0.6B`，或在启动前设置 `XIAOKA_QWEN_MODEL`。也可以设置 `XIAOKA_PYTHON` 指向安装了 PyTorch、Transformers 和 Accelerate 的 Python。双击 `start-xiaoka.bat` 后打开 `http://localhost:8123`。
