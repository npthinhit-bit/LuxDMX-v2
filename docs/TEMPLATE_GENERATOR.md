# Deterministic template generator

This document defines M0.4.4.3. The implementation is `tools/template_generator.py`; it resolves explicit UTF-8 `.ini` templates with a bounded inheritance contract and writes resolved template outputs plus the `luxdmx.generated-manifest.v1` manifest. It is a host-side generator only. It is not connected to PlatformIO/CMake and does not replace the current inline runtime templates yet.

## 1. Invocation

```bash
python3 tools/template_generator.py \
  --input-root templates \
  --template _base.ini \
  --template esp32s3_psram.ini \
  --template luxdmx_4uni.ini \
  --output-dir /tmp/luxdmx-template-output \
  --manifest-name generated-manifest.json \
  --generator-version 1 \
  --max-depth 8
```

`--template` is repeatable and identifies root templates relative to `--input-root`. Root order does not affect the output. Every root is sorted by UTF-8 byte order. A new output directory is required; an existing directory is refused to prevent stale files from being merged into a successful result.

## 2. Inheritance contract

A template is a UTF-8 `.ini` text file. Blank lines and lines whose first non-whitespace character is `#` are ignored. Every remaining line must contain a non-empty key and an `=` separator. Values are trimmed at both ends and may contain `=` after the first separator.

The reserved `extends` key may occur at most once per file. Its target is a canonical relative `.ini` path; the `.ini` suffix may be omitted, so `extends=_base` resolves to `_base.ini` under the same input root. Parent templates are resolved before the child. Parent values retain their insertion order, child values override an existing key in place, and new child keys append in source order.

| Rule | Behavior on violation |
|---|---|
| Root path | Must be canonical, relative POSIX path ending in `.ini` |
| Parent path | Must resolve under input root and point to a regular file |
| Encoding | Invalid UTF-8 is rejected |
| Key | Empty keys are rejected |
| Line format | Missing `=` is rejected |
| Same-file duplicate key | Rejected; child override of a parent key is allowed |
| Same-file duplicate `extends` | Rejected |
| Missing parent | Rejected |
| Cycle | Rejected with the complete chain |
| Depth | Rejected at the configured maximum, default 8 |
| Symlink | Rejected for template inputs and parents |
| File size | 64 MiB per template; 256 MiB total resolved input set |
| Resolved output | 64 MiB per output |

The output for each root is normalized to `key=value\n` lines without comments, blank lines or `extends=` directives. The output path preserves the root template's relative path. This normalized output is intentionally a generator artifact; it is not yet claimed to be the final runtime representation consumed by `config_schema.c`.

## 3. Manifest extension

The generator uses the same stable manifest identifier as the generic materializer and adds a `templates` array:

```json
{
  "schema": "luxdmx.generated-manifest.v1",
  "generator": {
    "name": "luxdmx.template-generator",
    "version": "1"
  },
  "inputs": [
    {"path": "_base.ini", "bytes": 2205, "sha256": "..."},
    {"path": "esp32s3_psram.ini", "bytes": 169, "sha256": "..."}
  ],
  "outputs": [
    {"path": "esp32s3_psram.ini", "source": "esp32s3_psram.ini", "bytes": 123, "sha256": "..."}
  ],
  "templates": [
    {
      "source": "esp32s3_psram.ini",
      "inheritance": ["_base.ini", "esp32s3_psram.ini"]
    }
  ],
  "manifest": {"path": "generated-manifest.json"},
  "status": "pass",
  "errors": []
}
```

`inputs` includes the complete transitive inheritance closure, sorted by relative path. `outputs` includes one normalized output per explicit root, sorted by root path. `templates[].inheritance` records the resolved base-first chain. The manifest has no timestamp, absolute path, host metadata, process environment or secret.

## 4. Current integration boundary

The current firmware runtime still contains inline board template strings in `components/lux_config/src/config_schema.c`. The tracked `templates/` files are not read by a build hook. M0.4.4.3 therefore proves parser and output determinism only; M0.4.4.5 must separately map the generated output into the real consumer and PlatformIO/CMake build phase.

The current web asset strings remain a separate decision. This generator does not read `webui/`, gzip assets or change `components/lux_web/`. A future web generator must first establish canonical source ownership and byte-level consumer compatibility.

## 5. Validation

Run:

```bash
python3 tools/test_template_generator.py
```

The suite covers base-first inheritance, parent override ordering, multiple roots, order-independent output, missing parent, cycles, duplicate directives/keys, malformed lines, depth, invalid UTF-8, size bounds, non-canonical paths and stale output refusal. A smoke run over the five current template files must be repeated twice with different root order and compare the complete output trees byte-for-byte.

## References

[1]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/tools/template_generator.py "Deterministic template generator"
[2]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/tools/test_template_generator.py "Template generator tests"
[3]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/M0.4.4_GENERATED_ASSETS_INVENTORY.md "Generated assets inventory"
[4]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/components/lux_config/src/config_schema.c "Current inline template consumer"
[5]: https://github.com/npthinhit-bit/LuxDMX-v2/blob/main/docs/GENERATED_MANIFEST.md "Generic generated manifest contract"
