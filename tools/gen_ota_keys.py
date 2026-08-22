#!/usr/bin/env python3
"""Generate an Ed25519 OTA signing key and the matching firmware header.

The private key is written only to the requested local path. Keep it outside
the repository or in a protected CI secret. The public header is safe to
commit and must match the key used by the release signer.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


def header_for(public_key: bytes) -> str:
    rows = [
        ", ".join(f"0x{byte:02x}" for byte in public_key[index : index + 8])
        for index in range(0, len(public_key), 8)
    ]
    return """#ifndef LUXDMX_OTA_KEY_H
#define LUXDMX_OTA_KEY_H

#include <stdint.h>

/* Generated public Ed25519 key. Never commit the corresponding private key. */
static const uint8_t OTA_PUBLIC_KEY[32] = {
    %s,
};

#endif
""" % "\n    ".join(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--private", required=True, type=Path, help="output PKCS#8 PEM path")
    parser.add_argument(
        "--public-header",
        required=True,
        type=Path,
        help="output components/lux_net/include/ota_key.h path",
    )
    args = parser.parse_args()
    if args.private.exists():
        raise SystemExit(f"refusing to overwrite existing private key: {args.private}")

    private_key = Ed25519PrivateKey.generate()
    public_key = private_key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw
    )
    pem = private_key.private_bytes(
        serialization.Encoding.PEM,
        serialization.PrivateFormat.PKCS8,
        serialization.NoEncryption(),
    )
    args.private.parent.mkdir(parents=True, exist_ok=True)
    args.private.write_bytes(pem)
    args.private.chmod(0o600)
    args.public_header.parent.mkdir(parents=True, exist_ok=True)
    args.public_header.write_text(header_for(public_key), encoding="utf-8")
    print(f"private key: {args.private} (keep secret)")
    print(f"public header: {args.public_header}")
    print(f"public key: {public_key.hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
