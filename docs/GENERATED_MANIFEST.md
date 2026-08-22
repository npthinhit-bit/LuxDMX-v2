# Generated manifest v1

This document defines the host-side contract introduced by M0.4.4.2. The implementation is `tools/generate_manifest.py`; it is a pure, dependency-free materializer for an explicit input set. It does not parse board-template inheritance, embed web assets, or integrate with PlatformIO yet. Those responsibilities remain later work packages.

## 1. CLI

The CLI requires an input root, one or more explicit relative input paths, and a new output directory:

```bash
python3 tools/generate_manifest.py \
  --input-root . \
  --input templates/_base.ini \
  --input templates/esp32s3_psram.ini \
  --output-dir /tmp/luxdmx-generated \
  --manifest-name generated-manifest.json \
  --generator-version 1
```

The input order does not affect the result. Paths are normalized as POSIX relative paths and sorted by UTF-8 byte order before materialization. The output directory must not already exist; refusing an existing directory prevents stale files from being silently accepted. A future build hook must provide an isolated output directory per environment.

## 2. Manifest schema

A successful run emits one UTF-8 JSON file with a final newline. Keys and arrays are emitted in stable order.

```json
{
  "schema": "luxdmx.generated-manifest.v1",
  "generator": {
    "name": "luxdmx.identity-generator",
    "version": "1"
  },
  "inputs": [
    {
      "path": "templates/_base.ini",
      "bytes": 2205,
      "sha256": "..."
    }
  ],
  "outputs": [
    {
      "path": "templates/_base.ini",
      "source": "templates/_base.ini",
      "bytes": 2205,
      "sha256": "..."
    }
  ],
  "manifest": {
    "path": "generated-manifest.json"
  },
  "status": "pass",
  "errors": []
}
```

| Field | Contract |
|---|---|
| `schema` | Versioned identifier `luxdmx.generated-manifest.v1` |
| `generator.name` | Stable generator identity, not a filesystem path |
| `generator.version` | Explicit caller-controlled generator version |
| `inputs` | Sorted input records with relative path, byte count and SHA-256 |
| `outputs` | Sorted materialized records with relative output path, source path, byte count and SHA-256 |
| `manifest.path` | Manifest filename relative to the output directory |
| `status` | `pass` only after every input is copied and the manifest is written |
| `errors` | Empty array for success; failed runs return non-zero and do not publish an output directory |

The template generator uses the same schema identifier and adds a `templates` array. Each entry records the explicit root `source` and its resolved base-first `inheritance` chain. Generic materialization does not emit this optional array.

The manifest intentionally does not contain commit SHA, host information, current time, absolute paths, process environment, secrets or toolchain discovery. Those belong to other bounded reports such as the firmware artifact report.

## 3. Safety and determinism rules

The implementation accepts regular files only. Absolute paths, Windows backslashes, empty paths, `.`/`..` components, symlink inputs, missing inputs and resolved paths outside `--input-root` are rejected. Duplicate normalized paths are rejected. Individual inputs are bounded at 64 MiB and the complete input set at 256 MiB. Files are copied and hashed in 1 MiB chunks.

Generation occurs in a same-parent staging directory. The staging directory is removed on failure and published with an atomic directory replacement only after all input files and the manifest have been written. An existing output directory is always rejected instead of merged or overwritten.

The generic CLI copies bytes and therefore does not impose a text encoding on future binary assets. Template-specific UTF-8, `extends=` and semantic validation belong to M0.4.4.3. Web/static asset encoding, gzip metadata and consumer mapping belong to the conditional M0.4.4.4 package.

## 4. Validation requirements

The test suite is `python3 tools/test_generate_manifest.py`. It covers successful manifest creation, order-independent byte-identical double-run output, binary bytes, missing input, non-canonical/duplicate paths, symlink rejection, size bounds, existing output refusal, absolute paths and backslashes.

A future deterministic checker must compare the complete output tree and manifest bytes from two clean runs. This CLI is deliberately not a PlatformIO hook yet; integrating it into `platformio.ini` before a real template/web consumer is mapped would violate the M0.4.4.1 decision record.

## References

[1]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/tools/generate_manifest.py "Pure generated manifest CLI"
[2]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/tools/test_generate_manifest.py "Generated manifest tests"
[3]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/M0.4.4_GENERATED_ASSETS_INVENTORY.md "Generated assets inventory and decision record"
[4]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/CI_FIRMWARE_ARTIFACTS.md "Firmware artifact contract"
