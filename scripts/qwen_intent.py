import argparse
import json
import sys


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--model", required=True)
    parser.add_argument("--text", required=True)
    args = parser.parse_args()

    try:
        import torch
        from transformers import AutoModelForCausalLM, AutoTokenizer
    except Exception:
        print(json.dumps(fallback(args.text), ensure_ascii=False))
        return

    system = (
        "你是小卡智能体的意图分类器。只输出一个 JSON 对象，不要解释。"
        "字段：intent, confidence, reply, tool, args。"
        f"intent 只能是：{', '.join(INTENTS)}。"
        "媒体控制：下一首/切歌=media_next；上一首=media_previous；暂停/继续/播放=media_play_pause；"
        "音量大=volume_up；音量小=volume_down；静音=volume_mute；打开音乐=open_music。"
    )
    messages = [
        {"role": "system", "content": system},
        {"role": "user", "content": args.text},
    ]

    tokenizer = AutoTokenizer.from_pretrained(args.model)
    model = AutoModelForCausalLM.from_pretrained(args.model, torch_dtype="auto")
    prompt = tokenizer.apply_chat_template(messages, tokenize=False, add_generation_prompt=True, enable_thinking=False)
    inputs = tokenizer([prompt], return_tensors="pt")
    output = model.generate(**inputs, max_new_tokens=180, do_sample=False)
    text = tokenizer.decode(output[0][len(inputs.input_ids[0]):], skip_special_tokens=True).strip()

    start = text.find("{")
    end = text.rfind("}")
    if start >= 0 and end > start:
        text = text[start:end + 1]
    try:
        data = json.loads(text)
    except Exception:
        data = fallback(args.text)

    if data.get("intent") not in INTENTS:
        data = fallback(args.text)
    print(json.dumps(data, ensure_ascii=False))


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(json.dumps({"intent": "unknown", "confidence": 0.0, "reply": "Qwen 意图理解暂时不可用，已回退。", "tool": "无需工具", "args": {}, "error": str(exc)}, ensure_ascii=False))
        sys.exit(0)
