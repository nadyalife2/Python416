import json

transcript_path = r"C:\Users\MSI\.gemini\antigravity\brain\ab5f9dc5-4083-42b3-9106-4d28be04a066\.system_generated\logs\transcript.jsonl"

with open("D:/esp32/audit_transcript.txt", "w", encoding="utf-8") as out:
    with open(transcript_path, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            try:
                item = json.loads(line)
                source = item.get("source", "")
                msg_type = item.get("type", "")
                content = item.get("content", "")
                if source == "USER_EXPLICIT":
                    out.write(f"\n==================== [STEP {idx}] USER ====================\n{content}\n")
                elif msg_type == "PLANNER_RESPONSE" and content and not item.get("tool_calls"):
                    out.write(f"\n-------------------- [STEP {idx}] ASSISTANT --------------------\n{content}\n")
            except Exception as e:
                pass
print("Done dumping audit_transcript.txt")
