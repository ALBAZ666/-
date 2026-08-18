import argparse
import json
import sys

if hasattr(sys.stdin, "reconfigure"):
    sys.stdin.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


INTENTS = [
    "media_next",
    "media_previous",
    "media_play_pause",
    "volume_up",
    "volume_down",
    "volume_mute",
    "open_music",
    "chat",
    "unknown",
]


def fallback(text):
    normalized = text.replace(" ", "")
    if any(w in normalized for w in ["下一首", "切歌", "换一首", "换歌"]):
        return {"intent": "media_next", "confidence": 0.82, "reply": "好的，给你切到下一首。", "tool": "系统媒体键：下一首", "args": {}}
    if any(w in normalized for w in ["上一首", "前一首", "上一曲"]):
        return {"intent": "media_previous", "confidence": 0.82, "reply": "好的，给你切回上一首。", "tool": "系统媒体键：上一首", "args": {}}
    if any(w in normalized for w in ["暂停", "继续", "播放"]):
        return {"intent": "media_play_pause", "confidence": 0.78, "reply": "好的，已切换播放状态。", "tool": "系统媒体键：播放/暂停", "args": {}}
    return {"intent": "unknown", "confidence": 0.2, "reply": "这个指令我还没学会。", "tool": "无需工具", "args": {}}


def parse_model_output(text, original):
    start = text.find("{")
    end = text.rfind("}")
    if start >= 0 and end > start:
        text = text[start:end + 1]
    try:
        data = json.loads(text)
    except Exception:
        return fallback(original)
    if data.get("intent") not in INTENTS:
        return fallback(original)
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    args = parser.parse_args()

    try:
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer
        tokenizer = AutoTokenizer.from_pretrained(args.model)
        model = AutoModelForCausalLM.from_pretrained(args.model, torch_dtype="auto")
        model.eval()
    except Exception as exc:
        print(json.dumps({"ready": False, "error": str(exc)}, ensure_ascii=False), flush=True)
        return

    print(json.dumps({"ready": True}, ensure_ascii=False), flush=True)

    system = (
        "你是小卡智能体的意图分类器。只输出一个 JSON 对象，不要解释。"
        "字段：intent, confidence, reply, tool, args。"
        f"intent 只能是：{', '.join(INTENTS)}。"
        "媒体控制：下一首/切歌=media_next；上一首=media_previous；暂停/继续/播放=media_play_pause；"
        "音量大=volume_up；音量小=volume_down；静音=volume_mute；打开音乐=open_music。"
    )

    for line in sys.stdin:
        request = {}
        try:
            request = json.loads(line)
            request_id = request.get("id")
            user_text = str(request.get("text", "")).strip()
            messages = [
                {"role": "system", "content": system},
                {"role": "user", "content": user_text},
            ]
            inputs = tokenizer.apply_chat_template(
                messages,
                tokenize=True,
                add_generation_prompt=True,
                enable_thinking=False,
                return_tensors="pt",
            )
            with torch.inference_mode():
                output = model.generate(**inputs, max_new_tokens=180, do_sample=False)
            generated = tokenizer.decode(
                output[0][inputs["input_ids"].shape[-1]:],
                skip_special_tokens=True,
            ).strip()
            result = parse_model_output(generated, user_text)
            result["id"] = request_id
            print(json.dumps(result, ensure_ascii=False), flush=True)
        except Exception as exc:
            print(json.dumps({
                "id": request.get("id"),
                "error": f"{type(exc).__name__}: {exc}",
            }, ensure_ascii=False), flush=True)


if __name__ == "__main__":
    main()
