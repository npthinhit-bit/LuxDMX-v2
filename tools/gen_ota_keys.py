#!/usr/bin/env python3
"""Generate an Ed25519 key pair for LuxDMX OTA firmware signing.

Outputs:
  tools/ota_private.pem  - PEM-encoded Ed25519 private key
  tools/ota_public.bin   - Raw 32-byte Ed25519 public key

The C array initializer for embedding the public key in ota_sign.cpp is
printed to stdout.

Requires: pip install cryptography

Usage: python3 tools/gen_ota_keys.py
"""
import sys
import pathlib

try:
    from cryptography.hazmat.primitives.asymmetric import ed25519
    from cryptography.hazmat.primitives import serialization
except ImportError:
    print("This script requires the cryptography library:", file=sys.stderr)
    print("  pip install cryptography", file=sys.stderr)
    sys.exit(1)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    tools_dir = root / "tools"
    tools_dir.mkdir(parents=True, exist_ok=True)

    private_key_path = tools_dir / "ota_private.pem"
    public_key_path = tools_dir / "ota_public.bin"

    private_key = ed25519.Ed25519PrivateKey.generate()

    private_pem = private_key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    private_key_path.write_bytes(private_pem)

    public_bytes = private_key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    public_key_path.write_bytes(public_bytes)

    print(f"Generated Ed25519 key pair:")
    print(f"  Private key: {private_key_path}")
    print(f"  Public key:  {public_key_path} ({len(public_bytes)} bytes)")
    print()
    print("Embed the public key in src/net/ota_sign.cpp as:")
    print()
    print("static const uint8_t OTA_PUBKEY[32] = {")
    for i in range(0, len(public_bytes), 8):
        row = public_bytes[i:i + 8]
        row_hex = ", ".join(f"0x{b:02x}" for b in row)
        print(f"    {row_hex},")
    print("};")
    print()
    print("WARNING: Keep ota_private.pem SECRET and store it offline.")


if __name__ == "__main__":
    main()