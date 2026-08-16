#!/usr/bin/env python3
"""Sign an OTA firmware image with an Ed25519 private key.

Computes the SHA-256 hash of the firmware, then signs that 32-byte hash
using pure Ed25519 (the hash is treated as the message to sign). The
64-byte signature is appended to the firmware image and the result is
written to the output file.

Requires: pip install cryptography

Usage:
    python3 tools/sign_ota.py <firmware.bin> <private_key.pem> <output.bin>
"""
import sys
import pathlib
import hashlib

try:
    from cryptography.hazmat.primitives.asymmetric import ed25519
    from cryptography.hazmat.primitives import serialization
except ImportError:
    print("This script requires the cryptography library:", file=sys.stderr)
    print("  pip install cryptography", file=sys.stderr)
    sys.exit(1)


def main():
    if len(sys.argv) != 4:
        print("Usage: python3 tools/sign_ota.py <firmware.bin> <private_key.pem> <output.bin>",
              file=sys.stderr)
        sys.exit(1)

    firmware_path = pathlib.Path(sys.argv[1])
    key_path = pathlib.Path(sys.argv[2])
    output_path = pathlib.Path(sys.argv[3])

    firmware = firmware_path.read_bytes()
    if not firmware:
        print("Error: firmware image is empty", file=sys.stderr)
        sys.exit(1)

    private_pem = key_path.read_bytes()
    private_key = serialization.load_pem_private_key(private_pem, password=None)

    if not isinstance(private_key, ed25519.Ed25519PrivateKey):
        print("Error: private key is not an Ed25519 key", file=sys.stderr)
        sys.exit(1)

    digest = hashlib.sha256(firmware).digest()

    signature = private_key.sign(digest)

    signed = firmware + signature
    output_path.write_bytes(signed)

    print(f"Firmware:  {firmware_path} ({len(firmware)} bytes)")
    print(f"Key:       {key_path}")
    print(f"Output:    {output_path} ({len(signed)} bytes)")
    print()
    print(f"SHA-256:   {digest.hex()}")
    print(f"Signature: {signature.hex()}")


if __name__ == "__main__":
    main()