import asyncio
import os
import sqlite3
import re
from telethon import TelegramClient
from telethon.tl.types import DocumentAttributeFilename, MessageMediaDocument
from telethon.errors.rpcerrorlist import FloodWaitError, UsernameInvalidError

# Database Setup
DB_FILE = 'telegram_data.db'

def init_db():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS entities (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            chat_id INTEGER,
            chat_username TEXT,
            message_id INTEGER,
            file_name TEXT,
            file_type TEXT,
            url TEXT,
            size INTEGER,
            downloaded INTEGER DEFAULT 0,
            UNIQUE(chat_id, message_id, file_type, url)
        )
    ''')
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS known_chats (
            username TEXT PRIMARY KEY,
            scanned INTEGER DEFAULT 0
        )
    ''')
    conn.commit()
    conn.close()

def save_file_info(chat_id, chat_username, message_id, file_name, file_type, size):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('''
            INSERT OR IGNORE INTO entities (chat_id, chat_username, message_id, file_name, file_type, size, url)
            VALUES (?, ?, ?, ?, ?, ?, '')
        ''', (chat_id, chat_username, message_id, file_name, file_type, size))
        conn.commit()
        conn.close()
    except Exception as e:
        print(f"DB Error: {e}")

def save_link_info(chat_id, chat_username, message_id, url):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('''
            INSERT OR IGNORE INTO entities (chat_id, chat_username, message_id, file_type, url)
            VALUES (?, ?, ?, ?, ?)
        ''', (chat_id, chat_username, message_id, 'link', url))
        conn.commit()
        conn.close()
    except Exception as e:
        print(f"DB Error: {e}")

def add_known_chat(username):
    try:
        conn = sqlite3.connect(DB_FILE)
        cursor = conn.cursor()
        cursor.execute('INSERT OR IGNORE INTO known_chats (username) VALUES (?)', (username,))
        conn.commit()
        conn.close()
    except Exception as e:
        pass

def get_unscanned_chats():
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('SELECT username FROM known_chats WHERE scanned = 0 LIMIT 10')
    chats = [row[0] for row in cursor.fetchall()]
    conn.close()
    return chats

def mark_chat_scanned(username):
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('UPDATE known_chats SET scanned = 1 WHERE username = ?', (username,))
    conn.commit()
    conn.close()


# Telegram Crawler logic
try:
    API_ID = int(os.environ.get('TG_API_ID', '0'))
except ValueError:
    API_ID = 0
API_HASH = os.environ.get('TG_API_HASH', '')

if API_ID == 0 or not API_HASH:
    print("Please set TG_API_ID and TG_API_HASH environment variables.")
    print("You can get them from https://my.telegram.org")
    exit(1)

client = TelegramClient('spider_session', API_ID, API_HASH)

# Regex to find links (including t.me)
URL_REGEX = re.compile(r'(https?://[^\s]+|t\.me/[^\s]+|@[a-zA-Z0-9_]+)')

async def scan_chat(chat_username):
    print(f"Scanning chat: {chat_username}")
    try:
        chat = await client.get_entity(chat_username)

        async for message in client.iter_messages(chat, limit=1000): # Limit to recent 1000 for safety, can be increased

            # 1. Look for links and new chats
            if message.text:
                urls = URL_REGEX.findall(message.text)
                for url in urls:
                    save_link_info(chat.id, chat_username, message.id, url)

                    # If it's a telegram link, add it to our spider queue
                    if 't.me/' in url:
                        potential_username = url.split('t.me/')[-1].split('/')[0].split('?')[0]
                        if potential_username and potential_username.replace('_', '').isalnum():
                            add_known_chat(potential_username)
                    elif url.startswith('@'):
                        potential_username = url[1:]
                        if potential_username.replace('_', '').isalnum():
                            add_known_chat(potential_username)

            # 2. Look for forwarded messages (new chats)
            if message.fwd_from and message.fwd_from.from_id:
                # We can't always get usernames directly from from_id easily without another request,
                # but if we can, we should. Skipping complex resolving for this simple spider.
                pass

            # 3. Look for files (.zip, .rar, documents)
            if message.media and isinstance(message.media, MessageMediaDocument):
                doc = message.media.document
                file_name = "unknown_document"
                file_ext = ""

                for attr in doc.attributes:
                    if isinstance(attr, DocumentAttributeFilename):
                        file_name = attr.file_name
                        if '.' in file_name:
                            file_ext = file_name.split('.')[-1].lower()
                        break

                # Check if it's a desired file type
                if file_ext in ['zip', 'rar', 'pdf', 'doc', 'docx', 'xls', 'xlsx'] or (not file_ext and doc.mime_type != 'application/x-tgsticker'):
                    save_file_info(chat.id, chat_username, message.id, file_name, file_ext or 'doc', doc.size)

        mark_chat_scanned(chat_username)

    except FloodWaitError as e:
        print(f"Flood wait! Must sleep for {e.seconds} seconds.")
        await asyncio.sleep(e.seconds)
    except UsernameInvalidError:
        print(f"Invalid username: {chat_username}")
        mark_chat_scanned(chat_username)
    except ValueError:
         print(f"Could not find entity: {chat_username}. Might be private or non-existent.")
         mark_chat_scanned(chat_username)
    except Exception as e:
        print(f"Error scanning {chat_username}: {e}")
        mark_chat_scanned(chat_username)


async def main():
    init_db()

    # Add initial seed chats if db is empty
    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute('SELECT COUNT(*) FROM known_chats')
    count = cursor.fetchone()[0]
    if count == 0:
        # Example seeds, replace with what the user wants to start with
        seeds = ['telegram', 'durov']
        for s in seeds:
            add_known_chat(s)
    conn.close()

    await client.start()
    print("Spider started. Press Ctrl+C to stop.")

    try:
        while True:
            chats_to_scan = get_unscanned_chats()
            if not chats_to_scan:
                print("No more known chats to scan. Waiting for new ones or sleep...")
                await asyncio.sleep(60)
                continue

            for chat in chats_to_scan:
                await scan_chat(chat)
                await asyncio.sleep(2) # Be gentle with the API
    except KeyboardInterrupt:
        print("Stopping spider.")

if __name__ == '__main__':
    asyncio.run(main())
