import argparse
import json
import sys

if hasattr(sys.stdin, "reconfigure"):
    sys.stdin.reconfigure(encoding="utf-8", errors="replace")
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


INTENTS = [
    "song_play",
    "song_favorite",
    "song_query",
    "navigate",
    "course_filter",
    "account_switch",
    "account_create",
    "version_switch",
    "microphone_off",
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
        "你是面向老年人和儿童的中文语音助手小卡。理解用户的每一句话，并给出简短、自然、尊重的中文回答。"
        "只输出一个 JSON 对象，不要输出思考过程或其他文字。"
        "字段：intent, confidence, reply, tool, args。"
        f"intent 只能是：{', '.join(INTENTS)}。"
        "可播放歌曲：最炫民族风、月亮之上、甜蜜蜜、自由飞翔、荷塘月色、快乐拉丁、活力街舞、阳光体操、民族小舞者。"
        "指定或想听歌曲用 song_play，args.song 填歌名（用户说\"进入音乐/放歌/想听歌\"但没点歌名时，args.song 可不填，自动播一首）；收藏歌曲用 song_favorite，args.song 填歌名；"
        "下一首/换一首用 media_next；上一首用 media_previous；暂停、继续或播放控制用 media_play_pause，args.action 填 pause 或 play；"
        "打开页面用 navigate，args.screen 只能填 home/list/achieve/profile/settings/kids/favorites/volume/display/privacy/about；"
        "退出音乐/停止播放/返回首页/回到主页/退出当前页面用 navigate，screen=home；"
        "入门、进阶、热门、最新课程用 course_filter，args.filter 填对应分类；"
        "切换已有账号用 account_switch，args.user 填姓名；用户说我是/我叫某人或新增账号用 account_create，args.user 填姓名；"
        "切换少儿版或广场舞版用 version_switch，args.version 填 kids 或 elderly；关闭麦克风用 microphone_off；"
        "询问收藏或最近歌曲用 song_query。不能执行但可以回答的问题用 chat，并直接在 reply 回答。"
        "无法判断才用 unknown，reply 要礼貌询问用户想做什么。"
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
