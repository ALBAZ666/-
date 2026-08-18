const $ = id => document.getElementById(id);

const state = {
  monitoring: false,
  awake: false,
  recognition: null,
  restartTimer: null,
  sleepTimer: null,
  speechSupported: false
};

const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;

function log(tag, text) {
  const row = document.createElement('div');
  row.className = 'line';
  row.innerHTML = `<span class="tag">${escapeHtml(tag)}</span><span>${escapeHtml(text)}</span>`;
  $('log').appendChild(row);
  $('log').scrollTop = $('log').scrollHeight;
}

function escapeHtml(text) {
  return String(text).replace(/[&<>"']/g, ch => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#039;'
  }[ch]));
}

function setPanel(title, sub, transcript) {
  $('voiceTitle').textContent = title;
  $('voiceSub').textContent = sub;
  if (transcript !== undefined) $('voiceTranscript').textContent = transcript || '等待语音...';
}

function setMonitoring(on) {
  state.monitoring = on;
  $('monitorState').textContent = on ? '监听中' : '未开启';
  $('micFab').classList.toggle('on', on);
  $('voicePanel').classList.toggle('show', on || state.awake);
}

function setAwake(on) {
  state.awake = on;
  $('micFab').classList.toggle('awake', on);
  if (on) {
    setPanel('小卡已唤醒', '请继续说你的指令', '');
    clearTimeout(state.sleepTimer);
    state.sleepTimer = setTimeout(() => {
      state.awake = false;
      $('micFab').classList.remove('awake');
      setPanel('小卡待命中', '说“小卡小卡”再次唤醒我', '');
      log('休眠', '长时间没有新指令，已回到待命状态');
    }, 12000);
  }
}

function speak(text) {
  if (!('speechSynthesis' in window)) return;
  window.speechSynthesis.cancel();
  const utter = new SpeechSynthesisUtterance(text);
  utter.lang = 'zh-CN';
  utter.rate = 1;
  utter.pitch = 1;
  window.speechSynthesis.speak(utter);
}

function stripWakeWord(text) {
  return String(text || '').replace(/小卡小卡|小卡/g, '').trim();
}

function containsWakeWord(text) {
  return /小卡小卡|小卡/.test(String(text || ''));
}

function setupRecognition() {
  if (!SpeechRecognition) {
    state.speechSupported = false;
    setPanel('当前浏览器不支持语音识别', '请用 Edge 或 Chrome 打开 http://localhost:8123', '');
    log('提示', '浏览器没有 SpeechRecognition / webkitSpeechRecognition');
    return null;
  }

  state.speechSupported = true;
  const rec = new SpeechRecognition();
  rec.lang = 'zh-CN';
  rec.continuous = true;
  rec.interimResults = true;
  rec.maxAlternatives = 1;

  rec.onstart = () => {
    setMonitoring(true);
    setPanel('小卡待命中', '说“小卡小卡”唤醒我', '');
    log('监听', '麦克风监听已开启');
  };

  rec.onresult = event => {
    let finalText = '';
    let interimText = '';
    for (let i = event.resultIndex; i < event.results.length; i += 1) {
      const piece = event.results[i][0].transcript.trim();
      if (event.results[i].isFinal) finalText += piece;
      else interimText += piece;
    }
    const shown = finalText || interimText;
    if (shown) setPanel(state.awake ? '小卡已唤醒' : '小卡待命中', state.awake ? '正在听你的指令' : '说“小卡小卡”唤醒我', shown);
    if (finalText) handleSpeech(finalText);
  };

  rec.onerror = event => {
    log('识别错误', event.error || 'unknown');
    if (event.error === 'not-allowed') {
      setMonitoring(false);
      setPanel('麦克风权限被拒绝', '请允许浏览器使用麦克风', '');
    }
  };

  rec.onend = () => {
    if (!state.monitoring) return;
    clearTimeout(state.restartTimer);
    state.restartTimer = setTimeout(() => {
      try { rec.start(); } catch (e) {}
    }, 450);
  };

  return rec;
}

function startListening() {
  if (!state.recognition) state.recognition = setupRecognition();
  if (!state.recognition) return;
  $('voicePanel').classList.add('show');
  try {
    state.recognition.start();
  } catch (e) {
    log('监听', '识别器已经在运行');
  }
}

function stopListening() {
  state.monitoring = false;
  state.awake = false;
  clearTimeout(state.restartTimer);
  clearTimeout(state.sleepTimer);
  $('micFab').classList.remove('on', 'awake');
  $('voicePanel').classList.remove('show');
  $('monitorState').textContent = '未开启';
  if (state.recognition) {
    try { state.recognition.stop(); } catch (e) {}
  }
  log('监听', '麦克风监听已关闭');
}

function handleSpeech(text) {
  log('ASR', text);

  if (!state.awake && containsWakeWord(text)) {
    setAwake(true);
    const command = stripWakeWord(text);
    speak(command ? '我在，马上处理。' : '我在。');
    log('唤醒', '检测到“小卡小卡”');
    if (command) runAgent(command);
    return;
  }

  if (state.awake) {
    const command = stripWakeWord(text);
    if (command) runAgent(command);
    else speak('我在，请说指令。');
  }
}

async function runAgent(text) {
  clearTimeout(state.sleepTimer);
  setPanel('小卡正在理解', '本地 Agent 处理中', text);
  log('用户', text);

  try {
    const res = await fetch('/api/agent', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ text })
    });
    const data = await res.json();
    if (!data.ok) throw new Error(data.error || 'Agent failed');

    log('理解', `${data.intent} ｜ ${data.engine}${data.qwenError ? ' ｜ Qwen 回退：' + data.qwenError : ''}`);
    log('工具', `${data.tool} ｜ ${data.toolResult && data.toolResult.ok ? '完成' : '跳过或失败'}`);
    log('小卡', data.reply);
    setPanel('小卡执行完成', data.tool, data.reply);
    speak(data.reply);
  } catch (e) {
    log('错误', e.message);
    setPanel('小卡遇到问题', '后端服务没有正常响应', e.message);
    speak('我这边执行失败了，请稍后再试一次。');
  } finally {
    setAwake(false);
  }
}

async function refreshHealth() {
  try {
    const res = await fetch('/api/health');
    const data = await res.json();
    $('engineState').textContent = data.useQwen ? 'Qwen + 规则' : '规则识别';
  } catch (e) {
    $('engineState').textContent = '后端未连接';
  }
}

$('micFab').addEventListener('click', () => {
  if (state.monitoring) stopListening();
  else startListening();
});

document.querySelectorAll('[data-command]').forEach(btn => {
  btn.addEventListener('click', () => {
    const cmd = btn.dataset.command;
    $('voicePanel').classList.add('show');
    setAwake(true);
    runAgent(stripWakeWord(cmd));
  });
});

$('clearLog').addEventListener('click', () => {
  $('log').innerHTML = '';
});

refreshHealth();
log('系统', '小卡界面已加载。点击右下角麦克风后，说“小卡小卡，下一首”。');
