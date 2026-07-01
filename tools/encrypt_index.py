#!/usr/bin/env python3
"""
encrypt_index.py — Encrypt a JSON index in Tinfoil format compatible with PortNX.

Usage:
    python encrypt_index.py <index.json> [--compress zstd|zlib|none] [--out output.tfl]

Dependencies:
    pip install cryptography zstandard
"""

import argparse
import json
import os
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

MAGIC = b"TINFOIL"
COMP_NONE = 0x00
COMP_ZSTD = 0x0D
COMP_ZLIB = 0x0E

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PUBLIC_KEY_PATH = os.path.join(SCRIPT_DIR, "public_key.pem")


def load_public_key():
    with open(PUBLIC_KEY_PATH, "rb") as f:
        return serialization.load_pem_public_key(f.read())


def compress_payload(data: bytes, method: str) -> tuple[bytes, int]:
    if method == "zstd":
        import zstandard as zstd
        cctx = zstd.ZstdCompressor(level=3)
        return cctx.compress(data), COMP_ZSTD
    elif method == "zlib":
        import zlib
        return zlib.compress(data, level=6), COMP_ZLIB
    else:
        return data, COMP_NONE


def pad_to_block(data: bytes, block: int = 16) -> bytes:
    rem = len(data) % block
    if rem == 0:
        return data
    return data + bytes(block - rem)


def encrypt_aes_ecb(key: bytes, plaintext: bytes) -> bytes:
    padded = pad_to_block(plaintext)
    cipher = Cipher(algorithms.AES(key), modes.ECB())
    enc = cipher.encryptor()
    return enc.update(padded) + enc.finalize()


def encrypt_index(json_path: str, compress: str, out_path: str):
    with open(json_path, "r", encoding="utf-8") as f:
        raw = json.dumps(json.load(f), separators=(",", ":")).encode("utf-8")

    compressed, comp_flag = compress_payload(raw, compress)
    inner_size = len(compressed)

    aes_key = os.urandom(16)

    pub_key = load_public_key()
    session_key = pub_key.encrypt(
        aes_key,
        padding.OAEP(
            mgf=padding.MGF1(algorithm=hashes.SHA256()),
            algorithm=hashes.SHA256(),
            label=None,
        ),
    )
    assert len(session_key) == 256

    ciphertext = encrypt_aes_ecb(aes_key, compressed)

    flag_byte = bytes([comp_flag])
    size_field = struct.pack("<Q", inner_size)

    out = MAGIC + flag_byte + session_key + size_field + ciphertext

    with open(out_path, "wb") as f:
        f.write(out)

    print(f"Written {len(out)} bytes to {out_path}")
    print(f"  JSON size  : {len(raw)} bytes")
    print(f"  Compressed : {inner_size} bytes ({compress})")
    print(f"  Payload    : {len(ciphertext)} bytes (AES-128-ECB padded)")


def main():
    parser = argparse.ArgumentParser(description="Encrypt a JSON index for PortNX")
    parser.add_argument("index", help="Path to the JSON index file")
    parser.add_argument(
        "--compress",
        choices=["zstd", "zlib", "none"],
        default="zstd",
        help="Compression method (default: zstd)",
    )
    parser.add_argument("--out", default=None, help="Output file path (default: <index>.tfl)")
    args = parser.parse_args()

    out_path = args.out or (os.path.splitext(args.index)[0] + ".tfl")
    encrypt_index(args.index, args.compress, out_path)


if __name__ == "__main__":
    main()
