import json
import sys

transcript_path = r"C:\Users\MSI\.gemini\antigravity\brain\78bd9840-086f-40e3-bba7-425b49adba23\.system_generated\logs\transcript.jsonl"

with open(transcript_path, "r", encoding="utf-8") as f:
    for idx, line in enumerate(f):
        try:
            item = json.loads(line)
            source = item.get("source", "")
            msg_type = item.get("type", "")
            content = item.get("content", "")
            if source == "USER_EXPLICIT":
                print(f"\n--- [STEP {idx}] USER ---:\n{content}\n")
            elif msg_type == "PLANNER_RESPONSE" and content and not item.get("tool_calls"):
                print(f"--- [STEP {idx}] ASSISTANT ({len(content)} chars) ---:\n{content[:300]}...\n")
        except Exception:
            pass
