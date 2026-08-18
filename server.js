const http = require('http');
const fs = require('fs');
const path = require('path');
const { execFile, spawn } = require('child_process');

const ROOT = __dirname;
const PUBLIC_DIR = path.join(ROOT, 'public');
const PORT = Number(process.env.PORT || 8123);
const USE_QWEN = process.env.XIAOKA_USE_QWEN !== '0';
const DEFAULT_PYTHON = 'C:\\Users\\Administrator\\AppData\\Local\\Programs\\Python\\Python313\\python.exe';
const PYTHON_BIN = process.env.XIAOKA_PYTHON || (fs.existsSync(DEFAULT_PYTHON) ? DEFAULT_PYTHON : 'python');
const QWEN_SERVICE_SCRIPT = path.join(ROOT, 'scripts', 'qwen_service.py');
const QWEN_MODEL_PATH = process.env.XIAOKA_QWEN_MODEL || 'E:\\AI\\Models\\Qwen3-0.6B';

let qwenWorker = null;
let qwenRequestId = 0;
const qwenPending = new Map();

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.ttf': 'font/ttf',
  '.wav': 'audio/wav',
  '.ico': 'image/x-icon'
};

const mediaKeys = {
  media_next: 0xB0,
  media_previous: 0xB1,
  media_stop: 0xB2,
  media_play_pause: 0xB3,
  volume_mute: 0xAD,
  volume_down: 0xAE,
  volume_up: 0xAF
};

function json(res, status, data) {
  const body = JSON.stringify(data);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET, POST, OPTIONS',
    'Access-Control-Allow-Headers': 'Content-Type',
    'Content-Length': Buffer.byteLength(body)
  });
  res.end(body);
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    let body = '';
    req.on('data', chunk => {
      body += chunk;
      if (body.length > 1024 * 64) {
        reject(new Error('request body too large'));
        req.destroy();
      }
    });
    req.on('end', () => resolve(body));
    req.on('error', reject);
  });
}

function serveStatic(req, res) {
  const url = new URL(req.url, 'http://localhost');
  let pathname = decodeURIComponent(url.pathname);
  if (pathname === '/') pathname = '/index.html';
  const target = path.resolve(PUBLIC_DIR, '.' + pathname);
  if (!target.startsWith(PUBLIC_DIR)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }
  fs.readFile(target, (err, data) => {
    if (err) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
      res.end('Not Found');
      return;
    }
    res.writeHead(200, {
      'Content-Type': MIME[path.extname(target).toLowerCase()] || 'application/octet-stream',
      'Cache-Control': 'no-cache'
    });
    res.end(data);
  });
}

function normalize(text) {
  return String(text || '')
    .toLowerCase()
    .replace(/\s+/g, '')
    .replace(/[，。！？、,.!?]/g, '');
}

function ruleIntent(text) {
  const raw = String(text || '').trim();
  const s = normalize(raw);

  if (!s) return intent('idle', '我在，等你说话。');
  if (/^(小卡|小卡小卡|在吗|你好)$/.test(s)) return intent('awake', '我在，请说。');

  if (hasAny(s, ['下一首', '下首', '切歌', '换一首', '换歌', '下一曲'])) {
    return intent('media_next', '好的，给你切到下一首。', '系统媒体键：下一首');
  }
  if (hasAny(s, ['上一首', '上首', '上一曲', '前一首'])) {
    return intent('media_previous', '好的，给你切回上一首。', '系统媒体键：上一首');
  }
  if (hasAny(s, ['暂停', '停一下', '先停', '暂停播放'])) {
    return intent('media_play_pause', '已暂停或切换播放状态。', '系统媒体键：播放/暂停');
  }
  if (hasAny(s, ['继续播放', '继续', '播放音乐', '开始播放', '放歌'])) {
    return intent('media_play_pause', '好的，继续播放。', '系统媒体键：播放/暂停');
  }
  if (hasAny(s, ['声音大一点', '音量大一点', '调大音量', '大声点'])) {
    return intent('volume_up', '音量调大一点。', '系统媒体键：音量加');
  }
  if (hasAny(s, ['声音小一点', '音量小一点', '调小音量', '小声点'])) {
    return intent('volume_down', '音量调小一点。', '系统媒体键：音量减');
  }
  if (hasAny(s, ['静音', '闭麦', '没声音'])) {
    return intent('volume_mute', '已切换静音。', '系统媒体键：静音');
  }
  if (hasAny(s, ['打开音乐', '打开播放器', '打开媒体播放器'])) {
    return intent('open_music', '我帮你打开系统音乐播放器。', '启动 Windows Media Player');
  }
  if (hasAny(s, ['你是谁', '介绍一下', '你叫什么'])) {
    return intent('chat', '我是小卡，你的本地语音智能体。你可以叫“小卡小卡”唤醒我。');
  }
  if (hasAny(s, ['能做什么', '会做什么', '帮助'])) {
    return intent('chat', '现在我能听唤醒词、切歌、暂停继续、调音量。后面还能接电脑文件、应用和本地 Qwen 推理。');
  }

  return intent('unknown', '这个指令我还没学会。你可以先试试：小卡小卡，下一首。');
}

function hasAny(text, words) {
  return words.some(word => text.includes(word));
}

function intent(name, reply, tool) {
  return {
    intent: name,
    confidence: name === 'unknown' ? 0.2 : 0.86,
    reply,
    tool: tool || '无需工具',
    args: {},
    engine: 'rules'
  };
}

function startQwenWorker() {
  if (!USE_QWEN || qwenWorker) return;

  qwenWorker = spawn(PYTHON_BIN, [QWEN_SERVICE_SCRIPT, '--model', QWEN_MODEL_PATH], {
    stdio: ['pipe', 'pipe', 'pipe'],
    windowsHide: true
  });

  let buffer = '';
  qwenWorker.stdout.on('data', chunk => {
    buffer += chunk.toString();
    const lines = buffer.split(/\r?\n/);
    buffer = lines.pop() || '';
    lines.filter(Boolean).forEach(line => {
      try {
        const message = JSON.parse(line);
        if (message.ready) {
          console.log('[小卡] Qwen 常驻模型已就绪');
          return;
        }
        const pending = qwenPending.get(message.id);
        if (!pending) return;
        clearTimeout(pending.timer);
        qwenPending.delete(message.id);
        pending.resolve(message.error ? { error: message.error } : { ...message, engine: 'qwen' });
      } catch (e) {
        console.warn('[小卡] Qwen 输出解析失败：', line.slice(0, 240));
      }
    });
  });

  qwenWorker.stderr.on('data', chunk => {
    const text = chunk.toString().trim();
    if (text) console.log('[Qwen]', text.slice(-500));
  });

  qwenWorker.on('error', err => {
    console.warn('[小卡] Qwen 进程启动失败，将回退规则识别：', err.message);
  });

  qwenWorker.on('close', code => {
    console.warn(`[小卡] Qwen 常驻进程结束（code=${code}），后续请求回退规则识别`);
    qwenWorker = null;
    for (const [id, pending] of qwenPending) {
      clearTimeout(pending.timer);
      pending.resolve({ error: 'Qwen 常驻进程已结束' });
      qwenPending.delete(id);
    }
  });
}

function runQwenIntent(text) {
  if (!USE_QWEN) return Promise.resolve(null);
  startQwenWorker();
  if (!qwenWorker || !qwenWorker.stdin.writable) {
    return Promise.resolve({ error: 'Qwen 进程不可用' });
  }

  return new Promise(resolve => {
    const id = ++qwenRequestId;
    const timer = setTimeout(() => {
      qwenPending.delete(id);
      resolve({ error: 'Qwen 响应超时' });
    }, 45000);
    qwenPending.set(id, { resolve, timer });
    qwenWorker.stdin.write(JSON.stringify({ id, text }) + '\n', err => {
      if (err) {
        clearTimeout(timer);
        qwenPending.delete(id);
        resolve({ error: err.message });
      }
    });
  });
}

function tapMediaKey(action) {
  const code = mediaKeys[action];
  if (!code) return Promise.resolve({ ok: true, skipped: true });

  const ps = `
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
public class MediaKey {
  [DllImport("user32.dll")]
  public static extern void keybd_event(int bVk, int bScan, int dwFlags, int dwExtraInfo);
  public static void Tap(int key) {
    keybd_event(key, 0, 0, 0);
    keybd_event(key, 0, 2, 0);
  }
}
"@
[MediaKey]::Tap(${code})
`;

  return new Promise(resolve => {
    execFile('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', ps], {
      timeout: 5000,
      windowsHide: true
    }, err => {
      resolve(err ? { ok: false, error: err.message } : { ok: true });
    });
  });
}

function openMusicPlayer() {
  const ps = `
$targets = @("mswindowsmusic:", "microsoft.windowsmusic:", "wmplayer.exe")
foreach ($target in $targets) {
  try {
    Start-Process $target -WindowStyle Hidden -ErrorAction Stop
    exit 0
  } catch {}
}
exit 1
`;
  return new Promise(resolve => {
    execFile('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-Command', ps], {
      timeout: 5000,
      windowsHide: true
    }, err => resolve(err ? { ok: false, error: err.message } : { ok: true }));
  });
}

async function executeTool(parsed) {
  if (mediaKeys[parsed.intent]) return tapMediaKey(parsed.intent);
  if (parsed.intent === 'open_music') return openMusicPlayer();
  return { ok: true, skipped: true };
}

async function handleAgent(req, res) {
  try {
    const body = await readBody(req);
    const payload = JSON.parse(body || '{}');
    const text = String(payload.text || '').trim();

    const qwen = await runQwenIntent(text);
    let parsed = qwen && !qwen.error ? qwen : ruleIntent(text);
    if (!parsed.intent || !parsed.reply) parsed = ruleIntent(text);
    const toolResult = await executeTool(parsed);

    json(res, 200, {
      ok: true,
      heard: text,
      intent: parsed.intent,
      confidence: parsed.confidence || 0,
      reply: parsed.reply,
      tool: parsed.tool || '无需工具',
      args: parsed.args || {},
      engine: parsed.engine || 'rules',
      qwenError: qwen && qwen.error ? qwen.error : '',
      toolResult
    });
  } catch (e) {
    json(res, 500, { ok: false, error: e.message });
  }
}

const server = http.createServer((req, res) => {
  if (req.method === 'OPTIONS') return json(res, 204, {});
  if (req.url.startsWith('/api/health')) {
    return json(res, 200, {
      ok: true,
      name: '小卡',
      useQwen: USE_QWEN,
      qwenWorker: Boolean(qwenWorker),
      modelPath: QWEN_MODEL_PATH
    });
  }
  if (req.url.startsWith('/api/agent') && req.method === 'POST') return handleAgent(req, res);
  return serveStatic(req, res);
});

server.listen(PORT, '127.0.0.1', () => {
  console.log('');
  console.log('小卡 Agent 已启动');
  console.log(`访问地址：http://localhost:${PORT}`);
  console.log(`Qwen 意图理解：${USE_QWEN ? '已开启' : '未开启（当前使用规则识别）'}`);
  console.log('按 Ctrl+C 停止');
  console.log('');
  startQwenWorker();
});
