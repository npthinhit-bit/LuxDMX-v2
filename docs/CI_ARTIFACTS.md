# CI logs and failure artifacts

LuxDMX-v2 CI writes diagnostics under `ci-artifacts/<job-or-environment>/`. The directory is job-local and is uploaded with `if: always()` so failure evidence remains available after a failed step.

| Job | Artifact name | Required files | Retention |
|---|---|---|---|
| Native unit | `diagnostics-native-unit` | `native-run.log`, safe revision/status/tool context | 14 days |
| Firmware matrix | `diagnostics-firmware-<env>` | `platformio.log`, safe revision/status/tool context | 14 days |
| Documentation/hygiene | `diagnostics-docs-hygiene` | `hygiene.log`, safe revision/status/tool context | 14 days |
| Package | `diagnostics-package` | `package.log`, safe revision/status/tool context | 30 days |
| Release | `diagnostics-release` | `release.log`, safe revision/status/tool context | 30 days |

The allowlist collector is `tools/ci_capture_context.py`. It records the commit revision, short worktree status, Python/CMake/CTest versions, runner platform, job label and an explicit environment policy. It never serializes the process environment, signing key, GitHub token, WiFi password, OTA password or any other secret.

Build/test commands must use `set -o pipefail` and `tee` so the command's exit status is preserved while the log is written. Diagnostic collection and upload use `if: always()`, but they must not use `continue-on-error` for the actual quality gate. A missing diagnostic directory or missing required log is an artifact failure, not a silent pass.

Firmware output artifacts and diagnostics are separate namespaces. `firmware-<env>` contains only build outputs intended for downstream packaging; `diagnostics-firmware-<env>` contains logs and metadata. This separation prevents a failed build from being mistaken for a valid firmware artifact.

Local validation commands are:

```bash
python3 tools/test_ci_capture_context.py
python3 tools/test_repository_hygiene.py
python3 tools/repository_hygiene.py --tracked-only
```

M0.4.2 does not claim that GitHub branch protection, production signing approval or HIL evidence has been configured. Those remain later gates in M0.4.6/M0.4.7 and the relevant OTA/release issues.
