#!/usr/bin/env python3
"""Verify final Release artifacts and retain exact implementation evidence."""
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess

package = Path(__file__).resolve().parent
repo = package.parents[2]
build = Path(os.environ.get("EQ_BUILD", "/private/tmp/nam-post-amp-eq-v1"))
def command(args):
    result = subprocess.run(args, cwd=repo, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f"{args}: {result.stdout}")
    return result.stdout.strip()

host = json.loads((build / "host-validation.json").read_text())
au_failure = build / "host-tests/au-sandbox-failure.log"
if au_failure.exists():
    shutil.copyfile(au_failure, package / "logs/au-sandbox-failure.log")
for format in ("APP", "VST3", "AU"):
    data = host["formats"][format]
    assert data["passed"]
    bundle = Path(data["bundle"])
    binary = bundle / "Contents/MacOS/NeuralAmpModeler"
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == data["binary_sha256"], "Artifact changed after native acceptance"
    data["signature_verification"] = command(["codesign", "--verify", "--deep", "--strict", "--verbose=2", str(bundle)])
    data["plist_verification"] = command(["plutil", "-lint", str(bundle / "Contents/Info.plist")])
    data["binary_type"] = command(["file", str(binary)])
    data["architectures"] = command(["lipo", "-archs", str(binary)])
    text = Path(data["log"]).read_text()
    data["active_response_max_abs_db"] = max(float(value) for value in re.findall(r"max_abs_db=([\deE+.-]+)", text))
    scalar = re.search(r"scalar linearity: relative_rms=([\deE+.-]+) relative_peak=([\deE+.-]+)", text)
    assert scalar
    data["delay_send_relative_rms_error"] = float(scalar[1])
    data["delay_send_relative_peak_error"] = float(scalar[2])
    target_log = package / "logs" / f"{format.lower()}-acceptance.log"
    shutil.copyfile(data["log"], target_log)
    data["retained_log"] = str(target_log.relative_to(repo))
    build_log = build / f"build-{format}" / "build.log"
    data["build_log"] = str(build_log)
    data["build_warning_lines"] = [line for line in build_log.read_text().splitlines() if "warning:" in line]

command(["git", "diff", "--check"])
project = "NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj"
lint = command(["plutil", "-lint", project])
source_audit = json.loads(command(["python3", str(package / "source-audit.py")]))
(package / "source-audit.json").write_text(json.dumps(source_audit, indent=2) + "\n")
(package / "logs/source-audit.log").write_text(json.dumps(source_audit, indent=2) + "\n")
(package / "host-validation.json").write_text(json.dumps(host, indent=2) + "\n")
tracked = command(["git", "diff", "--name-only"]).splitlines()
new = command(["git", "ls-files", "--others", "--exclude-standard"]).splitlines()
inventory = sorted(set(tracked + new + [str((package / "implementation-validation.json").relative_to(repo))]))
sources = [name for name in inventory if Path(name).suffix in (".h", ".cpp", ".mm", ".py", ".sh", ".pbxproj")]
result = {"status": "pass", "date": "2026-10-20", "timezone": "Europe/Berlin",
    "branch": command(["git", "branch", "--show-current"]),
    "base_commit": command(["git", "rev-parse", "HEAD"]),
    "hardware": {"machine": command(["sysctl", "-n", "hw.model"]),
                 "cpu": command(["sysctl", "-n", "machdep.cpu.brand_string"]),
                 "os": platform.platform()},
    "products": host, "project_lint": lint, "diff_check": "passed",
    "source_audit": source_audit, "files_added_or_changed": inventory,
    "source_sha256": {name: hashlib.sha256((repo / name).read_bytes()).hexdigest() for name in sources},
    "manual": {"visual_inspection": "pending user review", "musical_audition": "pending user review"},
    "resolved_environment_failures": [
        "Sandboxed initial Xcode build could not write org.llvm.clang/ModuleCache.noindex/Session.modulevalidation; authorized system-cache retry succeeded.",
        "Sandboxed AU runner failed AudioComponentRegister with null component; authorized registrar-service retry passed all three formats. Original assertion retained under build/host-tests/au-sandbox-failure.log.",
        "Python syntax checking initially attempted an unwritable user cache; PYTHONPYCACHEPREFIX under the isolated temporary build root passed."],
    "scope_limits": ["VST3 sample64 and AU float32 tested; no VST3 float32 claim.",
                     "Legacy whole-plugin first-use scratch warmed before RT audit; isolated EQ first callback is audited.",
                     "No human audition or visual inspection; no presets, musical tuning, vendor changes, commits or pushes."]}
launch = subprocess.run(["pgrep", "-fl", str(Path(host["formats"]["APP"]["bundle"]) / "Contents/MacOS/NeuralAmpModeler")],
                        text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
result["standalone_launch"] = {"verified_running": launch.returncode == 0,
                              "observed_process": launch.stdout.strip(), "visual_automation": False}
for filename, key in (("engine-validation.json", "engine"), ("cpu-profile.json", "cpu")):
    path = package / filename
    if not path.exists():
        raise RuntimeError(f"Missing final evidence: {path}")
    result[key] = json.loads(path.read_text())
assert result["engine"]["status"] == "pass"
for name, digest in result["engine"]["source_sha256"].items():
    assert hashlib.sha256((repo / name).read_bytes()).hexdigest() == digest, "DSP source changed after engine validation"
assert "RT_AUDIT violations=0" in result["cpu"]["rt_audit"]
(package / "implementation-validation.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps({"status": result["status"], "branch": result["branch"],
    "files": len(inventory), "native_response_max_abs_db": max(item["active_response_max_abs_db"] for item in host["formats"].values()),
    "report": str(package / "implementation-validation.json")}, indent=2))
