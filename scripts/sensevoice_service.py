import base64
import contextlib
import io
import json
import os
import re
import sys
import tempfile

if hasattr(sys.stdin, "reconfigure"):
    sys.stdin.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

MODEL = os.environ.get('XIAOKA_ASR_MODEL', r'E:\AI\Models\modelscope\models\iic--SenseVoiceSmall\snapshots\master')


def main():
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            from funasr import AutoModel
            model = AutoModel(model=MODEL, disable_update=True, device='cpu')
    except Exception as exc:
        print(json.dumps({"ready": False, "error": f"{type(exc).__name__}: {exc}"}, ensure_ascii=False), flush=True)
        return

    print(json.dumps({"ready": True}, ensure_ascii=False), flush=True)

    for line in sys.stdin:
        request = {}
        name = None
        try:
            request = json.loads(line)
            req_id = request.get("id")
            raw = request.get("audio", "")
            audio = base64.b64decode(raw)
            with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as f:
                f.write(audio)
                name = f.name
            with contextlib.redirect_stdout(io.StringIO()):
                result = model.generate(input=name, cache={})
            text = result[0].get('text', '') if result else ''
            text = re.sub(r'<\|[^>]+\|>', '', text).strip()
            print(json.dumps({"id": req_id, "ok": True, "text": text}, ensure_ascii=False), flush=True)
        except Exception as exc:
            print(json.dumps({
                "id": request.get("id"),
                "ok": False,
                "error": f"{type(exc).__name__}: {exc}",
            }, ensure_ascii=False), flush=True)
        finally:
            if name:
                try:
                    os.unlink(name)
                except Exception:
                    pass


if __name__ == "__main__":
    main()
