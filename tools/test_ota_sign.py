#!/usr/bin/env python3
"""Self-test for the host OTA signing contract."""

from __future__ import annotations

import hashlib
import tempfile
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from sign_ota_image import SIGNATURE_SIZE, load_private_key, sign_image


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="luxdmx-ota-test-") as directory:
        root = Path(directory)
        private = Ed25519PrivateKey.generate()
        private_path = root / "private.pem"
        private_path.write_bytes(
            private.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.PKCS8,
                serialization.NoEncryption(),
            )
        )
        image_path = root / "firmware.bin"
        signed_path = root / "firmware-signed.bin"
        image = bytes(range(251))
        image_path.write_bytes(image)

        sign_image(image_path, signed_path, private_path)
        signed = signed_path.read_bytes()
        assert signed[:-SIGNATURE_SIZE] == image
        assert len(signed[-SIGNATURE_SIZE:]) == SIGNATURE_SIZE
        load_private_key(private_path).public_key().verify(signed[-SIGNATURE_SIZE:], hashlib.sha256(image).digest())

        tampered = bytearray(signed)
        tampered[-1] ^= 0x01
        try:
            private.public_key().verify(bytes(tampered[-SIGNATURE_SIZE:]), hashlib.sha256(image).digest())
        except InvalidSignature:
            pass
        else:
            raise AssertionError("tampered signature was accepted")

        empty_path = root / "empty.bin"
        empty_path.write_bytes(b"")
        try:
            sign_image(empty_path, root / "empty-signed.bin", private_path)
        except ValueError:
            pass
        else:
            raise AssertionError("empty image was accepted")
    print("test_ota_sign: signed-image contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
