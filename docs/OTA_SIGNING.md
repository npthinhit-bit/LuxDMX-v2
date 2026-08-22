# LuxDMX OTA Signing Runbook

LuxDMX signed images use the contract `signed_image = firmware.bin || Ed25519(SHA256(firmware.bin))`. The device reads the final 64 bytes as the signature, hashes every preceding byte in 1 KiB flash reads, and verifies the digest with the embedded raw 32-byte Ed25519 public key.

## Provision a production key

Generate a key pair on an offline or otherwise protected maintainer workstation. The private key is written with mode `0600`; it must never be committed, uploaded as a build artifact, or placed in the firmware repository.

```sh
python3 -m pip install cryptography
python3 tools/gen_ota_keys.py \
  --private /secure/path/luxdmx-ota-private.pem \
  --public-header components/lux_net/include/ota_key.h
```

Review the generated public header, commit only that header, and retain the private PEM in the organization’s secret store. The current repository header is a placeholder and is not a production release key.

## Sign a release artifact locally

Build a named release environment, then sign its unsigned `firmware.bin` with the matching private key. Passing `--public-header` makes the tool fail if the private/public pair does not match.

```sh
pio run -e esp32dev_release
python3 tools/sign_ota_image.py \
  --key /secure/path/luxdmx-ota-private.pem \
  --public-header components/lux_net/include/ota_key.h \
  --input .pio/build/esp32dev_release/firmware.bin \
  --output /secure/path/luxdmx-esp32dev-firmware-signed.bin
```

The signed file is the only firmware file intended for the production OTA page. Bootloader and partition binaries remain separate flash artifacts.

## Configure tag CI

Store the exact PKCS#8 PEM contents as the repository secret `LUXDMX_OTA_PRIVATE_KEY`. A `v*` tag builds only the `*_release` environments, materializes the key in the ephemeral runner, verifies the key against `components/lux_net/include/ota_key.h`, signs each firmware image, and publishes only the signed release archive. If the secret is absent or mismatched, the release workflow fails closed.

Development environments define `OTA_SIGN_ENABLED=0` explicitly and accept unsigned images for local iteration. They must not be used to produce a production release. The release profiles define `OTA_SIGN_ENABLED=1`, and the bootloader rollback option is supplied through the tracked `sdkconfig.defaults`.

## Key rotation

Generate a new pair, replace the embedded public header, run the complete native and firmware validation gates, and publish the first release signed with the new private key only after the device fleet’s upgrade policy has been reviewed. Never place both old and new private keys in the repository. Hardware acceptance and deliberate bad-signature rollback tests remain required before declaring a key rotation production-complete.
