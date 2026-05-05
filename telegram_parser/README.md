# Telegram Spider & Downloader

This project contains a crawler and a downloader for Telegram. It can scan public chats for specific files (`.zip`, `.rar`, documents) and links, and allow you to download them on demand.

## Prerequisites

1. Get your Telegram API credentials:
   - Go to https://my.telegram.org and log in.
   - Go to "API development tools".
   - Create a new application (if you haven't already).
   - Get your `API_ID` and `API_HASH`.

2. Install dependencies:
   ```bash
   pip install -r requirements.txt
   ```

## Setup

Set your API credentials as environment variables.

### Linux/macOS:
```bash
export TG_API_ID="your_api_id"
export TG_API_HASH="your_api_hash"
```

### Windows (CMD):
```cmd
set TG_API_ID=your_api_id
set TG_API_HASH=your_api_hash
```

## Usage

### 1. Run the Crawler (Spider)

The crawler will scan known chats, look for `.zip`, `.rar`, documents, and links, save their metadata to a local SQLite database (`telegram_data.db`), and look for new channel links to scan next.

```bash
python crawler.py
```

*Note: The first time you run this, it will ask you to log in with your Telegram account (phone number and verification code).*

### 2. View Found Files

Use the downloader script to see what the crawler found.

List all:
```bash
python downloader.py list
```

Filter by type (e.g., zip):
```bash
python downloader.py list --type zip
```

### 3. Download a File

Use the ID from the list to download a specific file.

```bash
python downloader.py download <ID>
```
Example:
```bash
python downloader.py download 5
```

The file will be saved in the `downloads/` folder.
