#!/usr/bin/env python3
"""Read-only M1 preservation/enrollment/results checks; optional validation report.

Run from any directory. --write records validation.json beside this script.
The build directory contains logs from validate.sh and benchmark output.
"""
import argparse
import csv
import hashlib
import json
import re
import subprocess
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[5]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--build",type=Path,default=Path("/private/tmp/ah-drive-m1"))
    parser.add_argument("--write",action="store_true")
    args=parser.parse_args()
    def git(*words): return subprocess.check_output(["git",*words],cwd=ROOT)
    paths=git("ls-files","HoldsworthEngine/dsp","HoldsworthEngine/integration",
              "HoldsworthEngine/ui","NeuralAmpModeler/NeuralAmpModeler.cpp",
              "NeuralAmpModeler/NeuralAmpModeler.h").decode().splitlines()
    preserved={}
    for name in paths:
        data=(ROOT/name).read_bytes()
        assert data==git("show","HEAD:"+name), "Unexpected legacy change: "+name
        preserved[name]=hashlib.sha256(data).hexdigest()
    project=ROOT/"NeuralAmpModeler/projects/NeuralAmpModeler-macOS.xcodeproj/project.pbxproj"
    text=project.read_text()
    phases=re.findall(r"\t\t(\w+) /\* Sources \*/ = \{(.*?)\n\t\t\};",text,re.S)
    enrolled=[key for key,body in phases if "JRockettAHDriveProcessor.cpp in Sources" in body]
    assert enrolled==["D1A700012F0000010000000A"], enrolled
    # No new production includes/targets; runtime only uses fixed storage and
    # lock-free uint32 handoff. Sanitizers/allocation tracking provide runtime checks.
    source=(ROOT/"HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp").read_text()
    hot=source.split("void JRockettAHDriveProcessor::processBlock",1)[1]
    for word in ("malloc(","new ","mutex","lock_guard","throw ","printf(","std::vector"):
        assert word not in hot,word
    header=(ROOT/"HoldsworthEngine/dsp/JRockettAHDriveProcessor.h").read_text()
    assert "static_assert(std::atomic<std::uint32_t>::is_always_lock_free)" in header
    for name in ("Debug","Release"):
        assert (args.build/(name+"-tests.log")).read_text().endswith("All 270 HoldsworthEngine tests passed.\n")
    assert (args.build/"sanitizer-tests.log").read_text().endswith("All 270 HoldsworthEngine tests passed.\n")
    assert (args.build/"tsan-tests.log").read_text().endswith("All 1 HoldsworthEngine tests passed.\n")
    for suffix in ("app","vst3","component"):
        assert (args.build/"products"/("NeuralAmpModeler."+suffix)).is_dir()
    assert "error:" not in (args.build/"plugin-build.log").read_text()
    results=json.loads((HERE/"results.json").read_text())
    assert results["fidelity_cases"]==810 and results["worst_fidelity"]["residual_dbr"] < -75
    assert results["max_oracle_absolute_error"] < 2e-11
    assert results["measured_latency_samples"]==[32]*6
    for name in ("prototype.py","profile.json","m1/measure.py"):
        assert hashlib.sha256((HERE.parent/name).read_bytes()).hexdigest()==results[name+"_sha256"]
    timing=list(csv.DictReader((HERE/"realtime.csv").open()))
    assert len(timing)==84
    assert {int(row["fs"]) for row in timing}=={44100,48000,88200,96000,176400,192000}
    assert {int(row["frames"]) for row in timing}=={1,2,4,8,32,64,128}
    worst=max(timing,key=lambda r:float(r["p99_budget_percent"]))
    assert float(worst["p99_budget_percent"])<100
    outliers=[r for r in timing if float(r["max_us"]) > 1e6*int(r["frames"])/int(r["fs"])]
    data=dict(profile=results["profile"],scope="isolated M1; no live integration",commit=False,
              tests={"Debug":270,"Release":270,"ASan_UBSan":270,"ThreadSanitizer":1,
                     "focused_Drive":10,"strict_warnings":"passed","static_analysis":"passed",
                     "APP_VST3_AU_Release":"passed; Drive excluded","allocation_tracking":"zero",
                     "lock_contract":"lock-free uint32 SPSC; no locks in processor",
                     "ASan_options":"detect_leaks=0:halt_on_error=1 (platform LeakSanitizer unavailable)"},
              cpu=dict(cases=84,iterations_each=1000,worst_p99=worst,raw_maximum_deadline_outliers=outliers,
                       processor_bytes=5424,scope="isolated processor on this Mac, not a scheduler/whole-plugin guarantee"),
              source_preservation=preserved,oracle=results)
    if args.write:
        data["source_sha256"]={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest()
            for p in [ROOT/"HoldsworthEngine/dsp/JRockettAHDriveProcessor.cpp",
                      ROOT/"HoldsworthEngine/dsp/JRockettAHDriveProcessor.h",
                      ROOT/"HoldsworthEngine/dsp/JRockettAHDriveProfile.h",
                      ROOT/"HoldsworthEngine/tests/JRockettAHDriveProcessorTests.cpp",project,
                      HERE/"benchmark.cpp",HERE/"oracle_bridge.cpp",HERE/"validate.sh"]}
        (HERE/"validation.json").write_text(json.dumps(data,indent=2)+"\n")
    print("PASS: preserved",len(preserved),"legacy files; Drive enrolled only in tests; native/oracle/CPU validation.")
    print("Worst p99 budget percent:",worst["p99_budget_percent"],"raw maximum outliers:",len(outliers))


if __name__=="__main__": main()
