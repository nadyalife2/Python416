import json

transcript_path = r"C:\Users\MSI\.gemini\antigravity\brain\78bd9840-086f-40e3-bba7-425b49adba23\.system_generated\logs\transcript.jsonl"

with open("D:/esp32/steps_clean.txt", "w", encoding="utf-8") as out:
    with open(transcript_path, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            if idx >= 2500:
                try:
                    item = json.loads(line)
                    source = item.get("source", "")
                    msg_type = item.get("type", "")
                    content = item.get("content", "")
                    if source == "USER_EXPLICIT":
                        out.write(f"\n=== [STEP {idx}] USER ===\n{content}\n")
                    elif msg_type == "PLANNER_RESPONSE" and content and not item.get("tool_calls"):
                        out.write(f"=== [STEP {idx}] ASSISTANT ===\n{content}\n")
                except Exception:
                    pass
print("Done writing steps_clean.txt")
