#!/usr/bin/env python3
"""Independent byte-layout reconstruction; never imports/calls the C++ codec.

These are synthetic compatibility fixtures, not claims of historical DAW
captures. --check verifies the frozen bytes and their metadata without writing.
The root native-writer oracle records current real API state separately.
"""

import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import struct


HEADER = "###NeuralAmpModeler###"
MODEL = "/Users/音楽/Models/Amp Étude.nam"
IR = "D:\\Cab IRs\\箱 4×12.wav"
CURRENT = [-3.25, -72.5, 2.25, 6.75, 8.5, 1.125, 0.0, 1.0,
           0.0, 1.0, 9.5, 2.0, 0.375]
TEN = CURRENT[:8] + [1.0, 0.0]
TAIL = bytes.fromhex("deadbeef01020304")


def string_bytes(text):
    value = text.encode("utf-8")
    return struct.pack("<i", len(value)) + value


def plugin_bytes(version, values, model=MODEL, ir=IR):
    prefix = b"" if version is None else string_bytes(HEADER) + string_bytes(version)
    return (prefix + string_bytes(model) + string_bytes(ir) +
            b"".join(struct.pack("<d", value) for value in values))


def artifacts():
    files = {}
    records = []

    def add(name, payload, *, version, stored, expected, plugin_end,
            model=MODEL, ir=IR, envelope="plugin", bypass=None, suffix=b""):
        files[name] = payload
        record = {
            "file": name, "provenance": "synthetic independent layout reconstruction",
            "envelope": envelope, "version": version,
            "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
            "plugin_end": plugin_end, "model_path": model, "ir_path": ir,
            "stored_physical_values": stored, "expected_current_physical_values": expected,
        }
        if bypass is not None:
            record["framework_bypass_int32"] = bypass
        if suffix:
            record["framework_suffix_hex"] = suffix.hex()
        records.append(record)

    versions = [(None, "headerless-pre-0710"), ("0.7.9", "header-0.7.9"),
                ("0.7.10", "header-0.7.10"), ("0.7.11", "header-0.7.11"),
                ("0.7.12", "header-0.7.12"), ("0.7.13", "header-0.7.13"),
                ("0.7.14", "header-0.7.14"), ("0.8.0", "header-0.8.0")]
    for version, label in versions:
        count = 10 if version is None or version in ("0.7.9", "0.7.10", "0.7.11") else (
            12 if version in ("0.7.12", "0.7.13") else 13)
        stored = TEN if count == 10 else CURRENT[:count]
        expected = CURRENT[:]
        if count == 10:
            expected[8:] = [0.0, 0.0, 12.0, 1.0, 1.0]
        elif count == 12:
            expected[12] = 1.0
        payload = plugin_bytes(version, stored)
        add(label + ".plugin.bin", payload, version=version, stored=stored,
            expected=expected, plugin_end=len(payload))

    stored = TEN[:8] + [0.0, 1.0]
    expected = CURRENT[:8] + [1.0, 0.0, 12.0, 0.0, 1.0]
    payload = plugin_bytes("0.7.10", stored)
    add("header-0.7.10-outnorm-off.plugin.bin", payload, version="0.7.10",
        stored=stored, expected=expected, plugin_end=len(payload))

    payload = plugin_bytes("0.8.0", CURRENT)
    for bypass in (0, 1):
        add(f"current-bypass-{bypass}.vst3-state.bin", payload + struct.pack("<i", bypass),
            version="0.8.0", stored=CURRENT, expected=CURRENT, plugin_end=len(payload),
            envelope="VST3 component state", bypass=bypass)
    add("current-bypass-1-tail.vst3-state.bin", payload + struct.pack("<i", 1) + TAIL,
        version="0.8.0", stored=CURRENT, expected=CURRENT, plugin_end=len(payload),
        envelope="VST3 component state with synthetic opaque suffix", bypass=1, suffix=TAIL)

    defaults = [0.0, -80.0, 5.0, 5.0, 5.0, 0.0, 1.0, 1.0, 1.0, 0.0, 12.0, 1.0, 0.0]
    empty = plugin_bytes("0.8.0", defaults, "", "")
    add("current-empty-paths.plugin.bin", empty, version="0.8.0", stored=defaults,
        expected=defaults, plugin_end=len(empty), model="", ir="")

    # AU ClassInfo data is exactly plugin state, with identity in its enclosing
    # CF dictionary. Version is PLUG_VERSION_HEX, not a decimal semver encoding.
    fourcc = lambda text: int.from_bytes(text.encode("ascii"), "big")
    au = {"version": 0x00000800, "type": fourcc("aufx"),
          "subtype": fourcc("1YEo"), "manufacturer": fourcc("SDAa"),
          "name": "M0 synthetic", "data": payload}
    outer = plistlib.dumps(au, fmt=plistlib.FMT_XML, sort_keys=True)
    files["current-synthetic.aupreset"] = outer
    records.append({"file": "current-synthetic.aupreset",
                    "provenance": "synthetic CF dictionary reconstruction; not an AU/DAW capture",
                    "envelope": "AU ClassInfo XML property list", "bytes": len(outer),
                    "sha256": hashlib.sha256(outer).hexdigest(),
                    "data_fixture": "header-0.8.0.plugin.bin",
                    "identity": {key: au[key] for key in ("version", "type", "subtype", "manufacturer")}})

    manifest = {"schema": 1,
                "provenance": "independent Python struct/plistlib reconstruction from inspected source at eaa21571",
                "abi": "little-endian signed int32 length; raw UTF-8 bytes; IEEE754 binary64 physical values",
                "not_daw_captures": True, "fixtures": records}
    files["manifest.json"] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    directory = Path(__file__).resolve().parent
    files = artifacts()
    for name, payload in files.items():
        path = directory / name
        if args.check:
            if not path.exists() or path.read_bytes() != payload:
                raise SystemExit(f"fixture mismatch: {name}")
        else:
            path.write_bytes(payload)
    # Independent outer-envelope integrity check is useful on non-Apple hosts.
    au = plistlib.loads(files["current-synthetic.aupreset"])
    assert au["data"] == files["header-0.8.0.plugin.bin"]
    assert au["type"] == int.from_bytes(b"aufx", "big")
    print(f"{'verified' if args.check else 'generated'} {len(files)} deterministic fixture files")


if __name__ == "__main__":
    main()
