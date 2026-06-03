import argparse
import os
import sqlite3
import asyncio
from telethon import TelegramClient

DB_FILE = 'telegram_data.db'

def list_files(file_type=None):
    if not os.path.exists(DB_FILE):
        print("Database not found. Please run the crawler first.")
        return

    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()

    query = "SELECT id, chat_username, message_id, file_name, file_type, url, downloaded FROM entities"
    params = ()

    if file_type:
        query += " WHERE file_type = ?"
        params = (file_type,)

    cursor.execute(query, params)
    rows = cursor.fetchall()

    print(f"{'ID':<5} | {'Type':<6} | {'Status':<10} | {'Source':<20} | {'Name/URL'}")
    print("-" * 80)
    for row in rows:
        row_id, chat, msg_id, fname, ftype, url, downloaded = row
        status = "Downloaded" if downloaded else "Pending"
        source = f"{chat}/{msg_id}" if chat else "Unknown"
        name_or_url = fname if ftype != 'link' else url
        print(f"{row_id:<5} | {ftype:<6} | {status:<10} | {source:<20} | {name_or_url}")

    conn.close()

async def download_file(entity_id):
    if not os.path.exists(DB_FILE):
        print("Database not found. Please run the crawler first.")
        return

    try:
        API_ID = int(os.environ.get('TG_API_ID', '0'))
    except ValueError:
        API_ID = 0
    API_HASH = os.environ.get('TG_API_HASH', '')

    if API_ID == 0 or not API_HASH:
        print("Please set TG_API_ID and TG_API_HASH environment variables.")
        return

    conn = sqlite3.connect(DB_FILE)
    cursor = conn.cursor()
    cursor.execute("SELECT chat_username, message_id, file_name, file_type, url FROM entities WHERE id = ?", (entity_id,))
    row = cursor.fetchone()

    if not row:
        print(f"No entity found with ID {entity_id}")
        conn.close()
        return

    chat_username, message_id, file_name, file_type, url = row

    if file_type == 'link':
        print(f"Entity {entity_id} is a link: {url}")
        print("Links cannot be downloaded via this tool, just open them in a browser.")
        conn.close()
        return

    client = TelegramClient('spider_session', API_ID, API_HASH)
    await client.start()

    print(f"Attempting to download {file_name} from {chat_username} (Message ID: {message_id})...")

    try:
        chat = await client.get_entity(chat_username)
        message = await client.get_messages(chat, ids=message_id)

        if not message or not message.media:
            print("Message or media not found. It might have been deleted.")
        else:
            if not os.path.exists('downloads'):
                os.makedirs('downloads')

            safe_file_name = os.path.basename(file_name) if file_name else f"download_{entity_id}"

            path = await client.download_media(message, file=f"downloads/{safe_file_name}")
            print(f"Successfully downloaded to {path}")

            cursor.execute("UPDATE entities SET downloaded = 1 WHERE id = ?", (entity_id,))
            conn.commit()
    except Exception as e:
        print(f"Failed to download: {e}")
    finally:
        await client.disconnect()
        conn.close()


def main():
    parser = argparse.ArgumentParser(description="Telegram Crawler Downloader Tool")
    subparsers = parser.add_subparsers(dest='command', help='Commands')

    # List command
    list_parser = subparsers.add_parser('list', help='List found files and links')
    list_parser.add_argument('--type', type=str, help='Filter by type (e.g., zip, pdf, link)')

    # Download command
    download_parser = subparsers.add_parser('download', help='Download a file by ID')
    download_parser.add_argument('id', type=int, help='ID of the entity to download')

    args = parser.parse_args()

    if args.command == 'list':
        list_files(args.type)
    elif args.command == 'download':
        asyncio.run(download_file(args.id))
    else:
        parser.print_help()

if __name__ == '__main__':
    main()
