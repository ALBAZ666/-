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

## 本地 SenseVoice 语音识别（ASR 常驻）

服务端内置本地中文语音识别（SenseVoice），把语音转成文字。模型常驻加载，**识别速度约 2.5 秒**（旧版每次请求重新加载 936MB 模型要 1-2 分钟）。

- 脚本：`scripts/sensevoice_service.py`（常驻）、`scripts/sensevoice_asr.py`（单次）
- 接口：`POST /api/asr`，请求体 `{"audio": "<base64 的 WAV>"}`，返回 `{"ok": true, "text": "识别文字"}`
- 模型：默认 `E:\AI\Models\modelscope\models\iic--SenseVoiceSmall\snapshots\master`，可用环境变量 `XIAOKA_ASR_MODEL` 覆盖
- 依赖：Python + `funasr` + `torch` + `transformers`

## 安卓盒子（RK3588）接入

小卡跑在 **FriendlyELEC RK3588 安卓盒子**（Android 14）上，通过壳 App（`com.xiaoka.agent`，WebView 加载页面 + 原生语音桥 `AndroidVoice`）和电脑端服务端通信。

**录音链路**：盒子 USB 麦克风 → 壳 App `AudioRecord` 录音（48000Hz）→ `POST /api/asr` 服务端识别 → 回传文字 → 页面唤醒/执行 → 壳 App TTS 播报。

### 壳 App 关键修改（需重打包 APK）

1. **录音采样率 16000 → 48000**：USB 麦克风（UGREEN CM564）硬件只支持 48000Hz，用 16000 会录到空数据（FrmRdy=0）。
   - 用 `baksmali`/`smali` 反汇编 `classes.dex`，在 `MainActivity.smali` 的 `recordAndRecognize` 里把 `const/16 v6, 0x3e80` 改成 `const v6, 0xbb80`，再重新汇编。
2. 服务端地址：页面 `public/index.html` 的 `voiceApiBase` 指向电脑服务端 IP（如 `http://192.168.7.139:8123`），并打包进 APK 的 `assets/index.html`。

### 网络配置（重要）

盒子连的 WiFi（AKX）是**多 AP 漫游网络**，电脑和盒子会随机漫游到不同网段（5.x / 7.x），导致跨网段连不上。**已确认的稳定方案**：
- 电脑无线网卡**禁用漫游**（注册表 `RoamAggressiveness = 0`）
- 电脑设**静态 IP**（如 `192.168.7.139`，网关 `192.168.7.1`），避免 DHCP 变动
- 盒子保持在同一网段

### 服务端启动

```bat
set XIAOKA_USE_QWEN=1
set XIAOKA_PYTHON=C:\Users\Administrator\AppData\Local\Programs\Python\Python313\python.exe
set XIAOKA_QWEN_MODEL=E:\AI\Models\Qwen3-0.6B
npm start
```

启动后 Qwen 意图理解 + SenseVoice 语音识别都常驻就绪，盒子即可使用。
