# PortNX

Nintendo Switch homebrew for installing ports from a remote server catalog.

Ports are NSP/NSZ/XCI/XCZ files served via a Tinfoil-compatible JSON index.  
Files are streamed directly to the SD card or NAND without intermediate storage.

## Features

- Browse a remote port catalog over HTTP/HTTPS (Tinfoil index format, encrypted or plain)
- Stream-install NSZ via NCM + ES
- Install queue with per-item progress and session history
- Marks installed ports in the browse list (NCM check + session fallback for unnamed titles)
- Language: English / Português (BR) — switchable in Settings
- Visual style inspired by SwitchU (glass cards, navy gradient background)

## Requirements

- Nintendo Switch running Atmosphere CFW
- `prod.keys` accessible to the homebrew (required for NSZ/XCZ decryption via NCM)
- A server hosting a Tinfoil-compatible `index.json`

## Usage

1. Copy `PortNX.nro` to `sdmc:/switch/PortNX/PortNX.nro`
2. Launch via hbmenu
3. Open **Settings**, set the **Index URL** to your server's JSON catalog
4. Browse **Ports**, select a title, press A to install or download

Configuration is saved to `sdmc:/switch/PortNX/config.json`.  
Icons are cached to `sdmc:/switch/PortNX/icons/`.

## Building

Requires devkitPro with `devkitA64`, `libnx`, and the Switch portlibs.

```sh
make -j$(nproc)
```

Output: `PortNX.nro`

## Catalog format

Standard Tinfoil JSON index. The app also accepts encrypted indexes (TINFOIL magic header).  
To create encrypted indexes compatible with PortNX, see [`tools/`](tools/README.md).

Example minimal index:

```json
{
  "files": [
    {
      "url": "https://example.com/MyPort.nsz",
      "size": 1234567890
    }
  ]
}
```

## License

GNU General Public License v3.0 — see [LICENSE](LICENSE).

### Third-party libraries

| Library | License |
|---------|---------|
| [libnx](https://github.com/switchbrew/libnx) | ISC |
| [Borealis](https://github.com/natinusala/borealis) | GPLv3 |
| [mbedTLS](https://github.com/Mbed-TLS/mbedtls) | Apache 2.0 |
| [libcurl](https://curl.se/libcurl/) | curl (MIT-like) |
| [nlohmann/json](https://github.com/nlohmann/json) | MIT |
| [zstd](https://github.com/facebook/zstd) | BSD / GPLv2 |
| [zlib](https://zlib.net) | zlib |
