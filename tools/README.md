# PortNX — Encrypted Index Tools

## Requirements

```bash
pip install cryptography zstandard
```

## Usage

`public_key.pem` must be in the same folder as the script.

```bash
python encrypt_index.py index.json
python encrypt_index.py index.json --compress zstd --out index.tfl
python encrypt_index.py index.json --compress zlib  --out index.tfl
python encrypt_index.py index.json --compress none  --out index.tfl
```

Upload the `.tfl` to any HTTP/HTTPS server and add the URL to PortNX.

## index.json format

```json
{
  "files": [
    {"url": "https://yourserver.com/Game Title [TITLEID][v0].nsp", "size": 123456789}
  ],
  "directories": [
    "https://yourserver.com/subfolder/"
  ],
  "success": "Welcome to my server!"
}
```
