import sqlite3
import re

DB_PATH = "D:\\esp32\\test_mem.db"
conn = sqlite3.connect(DB_PATH)
cur = conn.cursor()
cur.execute("CREATE VIRTUAL TABLE IF NOT EXISTS dialog_fts USING fts5(role, content, timestamp)")
cur.execute("INSERT INTO dialog_fts (role, content, timestamp) VALUES (?, ?, ?)", ("user", "SpotPear ESP32-S3 проект робота", "12345"))
conn.commit()

query = "проект OR робота"
cur.execute("SELECT role, content FROM dialog_fts WHERE dialog_fts MATCH ? LIMIT 1", (query,))
res = cur.fetchall()
print("FTS test:", res)
conn.close()
