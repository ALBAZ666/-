import base64, json, os, sys, tempfile, contextlib, io

MODEL = os.environ.get('XIAOKA_ASR_MODEL', r'E:\AI\Models\modelscope\models\iic--SenseVoiceSmall\snapshots\master')
raw = sys.stdin.read().strip()
try:
    audio = base64.b64decode(raw)
    with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
        f.write(audio); name = f.name
    with contextlib.redirect_stdout(io.StringIO()):
        from funasr import AutoModel
        model = AutoModel(model=MODEL, disable_update=True, device='cpu')
        result = model.generate(input=name, cache={})
    text = result[0].get('text', '') if result else ''
    import re
    text = re.sub(r'<\|[^>]+\|>', '', text).strip()
    print(json.dumps({'ok': True, 'text': text}, ensure_ascii=False))
finally:
    try: os.unlink(name)
    except Exception: pass
