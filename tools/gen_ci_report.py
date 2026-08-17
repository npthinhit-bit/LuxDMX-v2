#!/usr/bin/env python3
"""Generate a detailed CI report from build artifacts and coverage data.

Called from .github/workflows/ci.yml in the `build-artifacts` job.
Reads:
  - firmware_artifacts/memory-*.json  (per-env build footprint)
  - coverage_artifact/coverage.xml    (gcovr/lcov coverage report)
  - coverage_artifact/coverage.txt    (gcovr summary, optional)
Writes:
  - reports/ci_report.md              (human-readable markdown report)
  - size_report.json                  (machine-readable consolidated report)
Prints the markdown report to stdout.
"""
import json
import os
import glob
import xml.etree.ElementTree as ET


def collect_memory_reports(artifact_dir):
    """Read all memory-*.json files from the artifact download directory."""
    mem_files = sorted(glob.glob(os.path.join(artifact_dir, "memory-*.json")))
    env_reports = []
    for mf in mem_files:
        try:
            with open(mf) as f:
                data = json.load(f)
            env_name = data.get(
                "env",
                os.path.basename(mf).replace("memory-", "").replace(".json", ""),
            )
            if "error" in data:
                env_reports.append((env_name, "error", data["error"]))
            else:
                mem = data.get("usage_pct", {})
                within = data.get("within_limits", False)
                flash_pct = mem.get("flash_pct", "?")
                ram_pct = mem.get("ram_pct", "?")
                status = "ok" if within else "THRESHOLD"
                detail = f"Flash {flash_pct}%, RAM {ram_pct}%"
                env_reports.append((env_name, status, detail))
        except Exception as e:
            env_name = os.path.basename(mf)
            env_reports.append((env_name, "error", str(e)))
    return env_reports


def collect_coverage(cov_dir):
    """Parse coverage.xml for line-rate percentage."""
    cov_pct = "N/A"
    cov_xml = os.path.join(cov_dir, "coverage.xml")
    if not os.path.isfile(cov_xml):
        cov_xml = "coverage.xml"
    if os.path.isfile(cov_xml):
        try:
            tree = ET.parse(cov_xml)
            root = tree.getroot()
            rate = float(root.get("line-rate", "0"))
            cov_pct = f"{rate * 100:.1f}%"
        except Exception:
            cov_pct = "parse error"
    return cov_pct


def read_coverage_summary(cov_dir):
    """Read TOTAL line from coverage.txt if available."""
    for p in [os.path.join(cov_dir, "coverage.txt"), "coverage.txt"]:
        if os.path.isfile(p):
            with open(p) as f:
                raw = [line.strip() for line in f.readlines()]
            lines = [line for line in raw if line[:5] in ("TOTAL", "-----")]
            return "\n".join(lines[:5])
    return ""


def build_report(env_reports, cov_pct, cov_summary):
    """Build the markdown report as a list of lines."""
    sha = os.environ.get("GITHUB_SHA", "")
    repo = os.environ.get("GITHUB_REPOSITORY", "")
    ref = os.environ.get("GITHUB_REF_NAME", "")
    event = os.environ.get("GITHUB_EVENT_NAME", "")
    server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")

    lines = []
    lines.append("# LuxDMX-V2 CI Report")
    lines.append("")
    sha_short = sha[:7] if sha else "N/A"
    commit_url = f"{server}/{repo}/commit/{sha}" if repo else "N/A"
    lines.append(f"**Run:** [{sha_short}]({commit_url})")
    lines.append(f"**Branch:** {ref}")
    lines.append(f"**Trigger:** {event}")
    lines.append("")

    # ── Job status table ──
    lines.append("## Job Status")
    lines.append("")
    lines.append("| Job | Details |")
    lines.append("|-----|---------|")
    lines.append("| Static Analysis | See job log |")
    lines.append("| Dependency Scan | See job log |")
    for env_name, status, detail in env_reports:
        icon = "OK" if status == "ok" else "FAIL" if status == "error" else "WARN"
        lines.append(f"| Build ({env_name}) | {icon} — {detail} |")
    lines.append("| Native Tests + Coverage | See job log |")
    lines.append("| Unity Tests | See job log |")
    lines.append("| OTA Sign/Verify | See job log |")
    lines.append("| Fuzz Test | See job log |")
    lines.append("")

    # ── Build / memory table ──
    lines.append("## Build & Memory Footprint")
    lines.append("")
    lines.append("| Environment | Status | Detail |")
    lines.append("|-------------|--------|--------|")
    for env_name, status, detail in env_reports:
        status_icon = (
            "PASS" if status == "ok"
            else "FAIL" if status == "error"
            else "THRESHOLD"
        )
        lines.append(f"| {env_name} | {status_icon} | {detail} |")
    if not env_reports:
        lines.append("| (artifacts unavailable) | N/A | build job may have failed |")
    lines.append("")

    # ── Coverage ──
    lines.append("## Code Coverage")
    lines.append("")
    lines.append(f"**Line coverage: {cov_pct}** (threshold: 70%)")
    if cov_summary:
        lines.append("")
        lines.append("```")
        lines.append(cov_summary)
        lines.append("```")
    lines.append("")

    # ── Overall summary ──
    total = len(env_reports)
    fail_count = sum(1 for _, s, _ in env_reports if s == "error")
    lines.append("## Summary")
    lines.append("")
    lines.append(f"- Build environments: {total} total, {fail_count} failed")
    lines.append(f"- Code coverage: {cov_pct}")
    lines.append(f"- Coverage threshold: 70%")
    lines.append("")

    return "\n".join(lines)


def main():
    artifact_dir = os.environ.get("ARTIFACT_DIR", "firmware_artifacts")
    cov_dir = os.environ.get("COV_DIR", "coverage_artifact")

    env_reports = collect_memory_reports(artifact_dir)
    cov_pct = collect_coverage(cov_dir)
    cov_summary = read_coverage_summary(cov_dir)

    report = build_report(env_reports, cov_pct, cov_summary)

    os.makedirs("reports", exist_ok=True)
    with open("reports/ci_report.md", "w") as f:
        f.write(report)

    # Also write size_report.json for programmatic consumption
    size_report = {}
    for env_name, status, detail in env_reports:
        mem_file = os.path.join(artifact_dir, f"memory-{env_name}.json")
        if os.path.isfile(mem_file):
            try:
                with open(mem_file) as f:
                    size_report[env_name] = json.load(f)
            except Exception:
                size_report[env_name] = {"error": "parse failed"}
    with open("size_report.json", "w") as f:
        json.dump(size_report, f, indent=2)

    print(report)
    print("--- Report written to reports/ci_report.md ---")


if __name__ == "__main__":
    main()
