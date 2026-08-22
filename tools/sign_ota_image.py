#!/usr/bin/env python3
"""Sign a LuxDMX OTA image using the repository's signed-image contract.

The input image must be the raw ESP-IDF firmware.bin without a trailing
signature. The output is input || Ed25519(SHA256(input)). Private keys are
accepted as PKCS#8 PEM or as 32 raw seed bytes. No private key is written.
"""

from __future__ import annotations

import argparse
import hashlib
import re
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

SIGNATURE_SIZE = 64
PRIVATE_SEED_SIZE = 32


def load_private_key(path: Path) -> Ed25519PrivateKey:
    data = path.read_bytes()
    if data.startswith(b"-----BEGIN"):
        key = serialization.load_pem_private_key(data, password=None)
        if not isinstance(key, Ed25519PrivateKey):
            raise ValueError(f"{path} does not contain an Ed25519 private key")
        return key
    if len(data) == PRIVATE_SEED_SIZE:
        return Ed25519PrivateKey.from_private_bytes(data)
    try:
        seed = bytes.fromhex(data.decode("ascii").strip())
    except (UnicodeDecodeError, ValueError) as exc:
        raise ValueError(
            f"{path} must be PKCS#8 PEM, 32 raw bytes, or 64 hex characters"
        ) from exc
    if len(seed) != PRIVATE_SEED_SIZE:
        raise ValueError(f"{path} hex seed must contain 32 bytes")
    return Ed25519PrivateKey.from_private_bytes(seed)


def load_public_header(path: Path) -> bytes:
    text = path.read_text(encoding="utf-8")
    values = re.findall(r"0x([0-9a-fA-F]{2})", text)
    public_key = bytes.fromhex("".join(values))
    if len(public_key) != 32:
        raise ValueError(f"{path} must contain exactly 32 public-key bytes")
    return public_key


def sign_image(
    input_path: Path,
    output_path: Path,
    key_path: Path,
    public_header: Path | None = None,
) -> None:
    image = input_path.read_bytes()
    if len(image) < 1:
        raise ValueError("input firmware image is empty")
    private_key = load_private_key(key_path)
    public_key_obj = private_key.public_key()
    public_key = public_key_obj.public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )
    if public_header is not None and public_key != load_public_header(public_header):
        raise ValueError("private signing key does not match the embedded ota_key.h")

    digest = hashlib.sha256(image).digest()
    signature = private_key.sign(digest)
    if len(signature) != SIGNATURE_SIZE:
        raise ValueError("Ed25519 signature has an unexpected size")
    public_key_obj.verify(signature, digest)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_bytes(image + signature)
    print(f"signed {input_path} -> {output_path} ({len(image)} + {len(signature)} bytes)")
    print(f"sha256={hashlib.sha256(image).hexdigest()}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--key", required=True, type=Path, help="Ed25519 private key file")
    parser.add_argument("--input", required=True, type=Path, help="unsigned firmware.bin")
    parser.add_argument("--output", required=True, type=Path, help="signed firmware output")
    parser.add_argument(
        "--public-header",
        type=Path,
        help="optional embedded ota_key.h; fail if it does not match the private key",
    )
    args = parser.parse_args()
    sign_image(args.input, args.output, args.key, args.public_header)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
