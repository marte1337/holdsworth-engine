#!/usr/bin/env python3
"""Compile actual-format acceptance runners from the isolated Release builds."""
import os
from pathlib import Path
import shlex
import subprocess

package = Path(__file__).resolve().parent
repo = package.parents[2]
build = Path(os.environ.get("TUNER_BUILD", "/private/tmp/nam-tuner-v1"))
developer = os.environ.get("DEVELOPER_DIR", "/Applications/Xcode.app/Contents/Developer")
env = dict(os.environ, DEVELOPER_DIR=developer)
cxx = subprocess.check_output(["xcrun", "--find", "clang++"], env=env, text=True).strip()
sdk = subprocess.check_output(["xcrun", "--show-sdk-path"], env=env, text=True).strip()
workdir = repo / "NeuralAmpModeler/projects"
output = build / "host-tests"
output.mkdir(parents=True, exist_ok=True)

def run(command, name):
    path = output / f"build-{name}.log"
    with path.open("w") as log:
        result = subprocess.run(command, cwd=workdir, env=env, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        raise SystemExit(f"Build {name} failed; see {path}")
    print(f"PASS build: {name}", flush=True)

frameworks = []
for framework in ("Cocoa", "Carbon", "CoreFoundation", "CoreData", "Foundation", "CoreServices",
                  "QuartzCore", "AppKit", "CoreMIDI", "CoreAudio", "AudioToolbox", "AudioUnit",
                  "Accelerate", "Metal", "MetalKit"):
    frameworks.extend(["-framework", framework])
base = [cxx, "-arch", "arm64", "-isysroot", sdk, "-mmacosx-version-min=12.0", "-pthread"]
vst = repo / "iPlug2/Dependencies/IPlug/VST3_SDK"
for format in ("APP", "VST3", "AU"):
    target = build / f"build-{format}/obj/NeuralAmpModeler-macOS.build/Release" / f"{format}.build/Objects-normal/arm64"
    response = next(path for path in target.glob("*common-args.resp") if "-std=c++20" in path.read_text())
    options = shlex.split(response.read_text())
    options = [option for option in options if option not in ("-fvisibility=hidden", "-fvisibility-inlines-hidden")]
    sources = [str(package / f"{format.lower()}-host.mm")]
    if format == "APP":
        objects = [path for path in (target / "NeuralAmpModeler.LinkFileList").read_text().splitlines()
                   if Path(path).name != "IPlugAPP_main.o"]
        sources += objects
    elif format == "VST3":
        sources += [str(vst / path) for path in ("pluginterfaces/base/funknown.cpp",
                    "pluginterfaces/base/coreiids.cpp", "public.sdk/source/vst/vstinitiids.cpp")]
    run(base + options + ["-O3", "-Wl,-export_dynamic", "-Wl,-dead_strip"] + sources + frameworks
        + ["-o", str(output / f"{format.lower()}-host")], format.lower())
run(base + ["-std=c++20", "-O2", "-dynamiclib", "-undefined", "dynamic_lookup",
            str(package / "rt-audit.cpp"), "-o", str(output / "rt-audit.dylib")], "rt-audit")
run(base + ["-std=c++20", "-O3", "-Wl,-export_dynamic", str(package / "capture-cpu.cpp"),
            "-o", str(output / "capture-cpu")], "capture-cpu")
