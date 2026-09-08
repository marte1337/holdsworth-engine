#!/usr/bin/env python3
"""Validate and deterministically materialize the TC BLD M0a documentary IR.

The script deliberately has no third-party dependencies. The JSON schema is
useful while editing, while this program owns cross-file semantics, immutable
service facts, executable ambiguity variants, and the independent freeze gate.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any


PROFILE_ID = "TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-M0A-001"
P_SM_SHA256 = "6d794b2ce05a93f53c579303a5b42682604e1c7922e14c064e30ea9175c0ac62"
P_SM_BYTES = 203201

DATA_FILES = (
    "manifest.json",
    "source-evidence.json",
    "service-components.json",
    "circuit.json",
    "assumptions.json",
    "configurations.json",
    "generic-profiles.json",
    "documentary-cross-check.json",
)
EXPECTED_FORMATS = {
    "manifest.json": "tc-bld-m0a-manifest-v1",
    "source-evidence.json": "tc-bld-source-evidence-v1",
    "service-components.json": "tc-bld-service-components-v1",
    "circuit.json": "tc-bld-documentary-circuit-v1",
    "assumptions.json": "tc-bld-assumptions-v1",
    "configurations.json": "tc-bld-configurations-v1",
    "generic-profiles.json": "tc-bld-generic-profiles-v1",
    "documentary-cross-check.json": "tc-bld-documentary-cross-check-v1",
}
EXPECTED_MANIFEST_FILES = {
    "service_values": "service-components.json",
    "topology": "circuit.json",
    "assumptions": "assumptions.json",
    "configurations": "configurations.json",
    "generic_profiles": "generic-profiles.json",
    "source_evidence": "source-evidence.json",
    "cross_check": "documentary-cross-check.json",
    "schema": "schema/tc-bld-documentary-ir-v1.schema.json",
    "validator": "tools/validate_m0a.py",
    "validator_tests": "tests/test_validate_m0a.py",
    "human_readme": "README.md",
}
IMPACT_AXES = {
    "boost_small_signal",
    "boost_large_signal",
    "distortion",
    "suppressor",
    "bypass",
}
IMPACT_VALUES = {"material", "possible", "control_only", "nonmaterial", "unknown"}
ALLOWED_OPERATIONS = {
    "bind_terminal",
    "link_nodes",
    "set_contact_table",
    "set_orientation",
    "select_profile",
    "select_boundary_profile",
    "set_device_state",
    "annotation_only",
}
VARIANT_CLASSES = {
    "executable",
    "documentary_no_electrical_operation",
    "evidence_acquisition_requirement",
    "rejected_interpretation",
}
NON_RUNNABLE_CLASSES = {"evidence_acquisition_requirement", "rejected_interpretation"}
DOCUMENTARY_STATUSES = {
    "confirmed",
    "ambiguous",
    "conflict",
    "unreadable",
    "not_depicted",
    "not_applicable",
}
UNCERTAIN_STATUSES = {"ambiguous", "conflict", "unreadable"}

REQUIRED_ASSUMPTIONS = {
    "A-MODE-CONTACTS",
    "A-Q1-STATE",
    "A-Q2-STATE",
    "A-C17-RETURN",
    "A-R13-BOUNDARY",
    "A-DOCUMENT-COMPOUND",
    "A-P1-TAPER",
    "A-P2-TAPER",
    "A-P3-TAPER",
    "A-P4-TAPER",
    "A-P5-TAPER",
    "A-4741-GENERIC",
    "A-SUPPLY-VREF",
    "A-SOURCE-LOAD",
    "A-XLR-PINOUT",
    "A-D17-IC2",
    "A-CAP-POLARITY-SIGNAL",
    "A-D14-D15-NUMBERING",
    "A-CONTROL-DIODE-POLARITY",
    "A-R33-R36-REVISION",
    "A-R35-SYMBOL",
    "A-JFET-DS-IDENTITY",
    "A-POWER-JACK-CONTACTS",
    "A-LAYOUT-READABILITY",
}

REQUIRED_MATERIAL_CHECKS = {
    "CHK-BOM-INVENTORY-VALUES",
    "CHK-LAYOUT-PLACEMENT",
    "CHK-RAILS",
    "CHK-VREF",
    "CHK-POWER-CONTACTS",
    "CHK-IC1-ELECTRICAL-PINS",
    "CHK-IC1-PACKAGE",
    "CHK-IC2-ELECTRICAL-PINS",
    "CHK-IC2-PACKAGE",
    "CHK-NREF-AUDIO",
    "CHK-MODE",
    "CHK-POT-ELECTRICAL",
    "CHK-POT-SERVICE",
    "CHK-POT-NUMERIC-LAWS",
    "CHK-DIODE-AUDIO",
    "CHK-DIODE-CONTROL",
    "CHK-D17-POPULATION",
    "CHK-Q1-Q2-ELECTRICAL",
    "CHK-Q3-Q6-ELECTRICAL",
    "CHK-Q3-Q6-IDENTITY",
    "CHK-CAP-ENDPOINTS",
    "CHK-CAP-VALUES",
    "CHK-CAP-POLARITY",
    "CHK-C17",
    "CHK-TONE",
    "CHK-SUPPRESSOR",
    "CHK-BYPASS",
    "CHK-OUTPUT",
    "CHK-XLR",
    "CHK-REVISIONS",
    "CHK-R33-R36",
    "CHK-R35-PRESET",
    "CHK-R13-TOPOLOGY",
    "CHK-R13-VALUE",
    "CHK-INPUT-MANUAL-CLAIM",
}

AUDIT_COVERAGE = (
    "exact_primary_source",
    "schematic_topology",
    "service_bom",
    "layout_artwork",
    "every_designator",
    "every_nominal_value",
    "active_device_pins",
    "diode_polarity",
    "transistor_orientation",
    "potentiometer_terminals",
    "switch_contacts",
    "rails_and_vref",
    "coupling_and_filter_capacitors",
    "output_and_load_path",
    "assumptions_and_variants",
    "deterministic_materialization",
)


class DuplicateKeyError(ValueError):
    """Raised when JSON contains an object member more than once."""


class MaterializationError(ValueError):
    """Raised for a selection that cannot produce a deterministic handoff."""


def expected_refs() -> list[str]:
    return (
        [f"R{i}" for i in range(1, 49)]
        + [f"C{i}" for i in range(1, 32)]
        + [f"P{i}" for i in range(1, 6)]
        + [f"Q{i}" for i in range(1, 7)]
        + [f"D{i}" for i in range(1, 19)]
        + ["DG1", "IC1", "IC2"]
    )


EXPECTED_RESISTORS = [
    ("6.8", "MOhm"), ("47", "kOhm"), ("47", "kOhm"), ("3.3", "MOhm"),
    ("1", "MOhm"), ("1", "MOhm"), ("220", "kOhm"), ("220", "kOhm"),
    ("1", "MOhm"), ("470", "kOhm"), ("1.5", "kOhm"), ("10", "kOhm"),
    ("1", "MOhm"), ("2.2", "kOhm"), ("470", "kOhm"), ("220", "Ohm"),
    ("33", "kOhm"), ("10", "kOhm"), ("3.3", "MOhm"), ("10", "kOhm"),
    ("4.7", "kOhm"), ("10", "kOhm"), ("1.5", "kOhm"), ("10", "kOhm"),
    ("100", "Ohm"), None, ("1", "MOhm"), ("47", "kOhm"),
    ("470", "kOhm"), ("10", "kOhm"), ("4.7", "kOhm"), ("1", "MOhm"),
    ("6.8", "MOhm"), ("680", "kOhm"), ("2.2", "MOhm"), ("6.8", "MOhm"),
    ("1", "MOhm"), ("1", "MOhm"), ("10", "kOhm"), ("47", "Ohm"),
    ("1.5", "kOhm"), ("1.5", "kOhm"), ("2.2", "kOhm"), ("1", "kOhm"),
    ("10", "kOhm"), ("10", "kOhm"), ("10", "MOhm"), ("22", "kOhm"),
]
EXPECTED_CAPACITORS = [
    ("22", "uF"), ("10", "uF"), ("1", "uF"), ("10", "nF"),
    ("1", "nF"), ("33", "nF"), ("1", "uF"), ("47", "nF"),
    ("220", "pF"), ("220", "pF"), ("1", "uF"), ("1", "uF"),
    ("4.7", "uF"), ("10", "nF"), ("10", "uF"), ("2.2", "uF"),
    ("10", "nF"), ("1", "uF"), ("100", "nF"), ("470", "pF"),
    ("10", "nF"), ("10", "nF"), ("47", "nF"), ("1", "uF"),
    ("22", "uF"), ("10", "nF"), ("100", "nF"), ("100", "nF"),
    ("1", "nF"), ("6.8", "nF"), ("220", "pF"),
]
EXPECTED_POTS = {
    "P1": ("47", "kOhm", "LOG"),
    "P2": ("22", "kOhm", "LIN"),
    "P3": ("100", "kOhm", "LIN"),
    "P4": ("22", "kOhm", "NEG.LOG"),
    "P5": ("470", "kOhm", "LOG"),
}
EXPECTED_PARTS = {
    "Q1": ("jfet", "BF245-A"),
    "Q2": ("jfet", "BF245-A"),
    "Q3": ("bjt_pnp", "BC558-B"),
    "Q4": ("bjt_npn", "BC548-B"),
    "Q5": ("bjt_npn", "BC548-B"),
    "Q6": ("bjt_npn", "BC548-B"),
    "D1": ("diode", "1N4001"),
    **{f"D{i}": ("diode", "1N4148") for i in range(2, 18)},
    "D18": ("led", "SBR 3431 red 3 mm"),
    "DG1": ("germanium_diode", "AA119"),
    "IC1": ("quad_op_amp", "4741 selected (GR1)"),
    "IC2": ("cmos_transistor_array", "HBF 4007 UBP"),
}
EXPECTED_ACTIVE_TERMINALS = {
    "Q1": {"D": "N_Q1_DRAIN", "G": "N_Q1_GATE", "S": "NREF_AUDIO"},
    "Q2": {"D": "N_Q2_DRAIN", "G": "N_Q2_GATE", "S": "OUT14"},
    "Q3": {"C": "N_Q3_COLLECTOR", "B": "N_Q3_BASE", "E": "VPLUS"},
    "Q4": {"C": "VPLUS", "B": "NREF_AUDIO", "E": "N_Q4_EMITTER"},
    "Q5": {"C": "N_Q5_COLLECTOR", "B": "N_Q5_BASE", "E": "N_Q5_EMITTER"},
    "Q6": {"C": "N_Q6_COLLECTOR", "B": "N_Q6_BASE", "E": "0V"},
    "D1": {"A": "BATTERY_POSITIVE_A", "K": "VPLUS"},
    "D2": {"A": "PWR_SENSE", "K": "POR"},
    "D3": {"A": "0V", "K": "VPLUS"},
    "D4": {"A": "VREF", "K": "N_ASYM_CLAMP"},
    "D5": {"A": "N_DETECTOR_OUT", "K": "N_IC1_2"},
    "D6": {"A": "N_IC1_2", "K": "N_DETECTOR_OUT"},
    "D7": {"A": "N_Q6_BASE", "K": "N_Q6_COLLECTOR"},
    "D8": {"A": "N_Q5_BASE", "K": "N_Q6_COLLECTOR"},
    "D9": {"A": "NRETURN", "K": "N_Q5_COLLECTOR"},
    "D10": {"A": "N_CTL2", "K": "NRETURN"},
    "D11": {"A": "N_Q2_GATE", "K": "OUT14"},
    "D12": {"A": "N_BYPASS_CONTROL", "K": "OUT14"},
    "D13": {"A": "N_Q1_GATE", "K": "N_BYPASS_CONTROL"},
    "D14": {"A": "N_IC1_5", "K": "N_IC1_6_SENSE"},
    "D15": {"A": "N_IC1_6_SENSE", "K": "N_IC1_5"},
    "D16": {"A": "N_SWITCH_REMOTE", "K": "EXTERNAL_BYPASS_2R"},
    "D17": {"A": "N_D17_A_UNRESOLVED", "K": "N_D17_K_UNRESOLVED"},
    "D18": {"A": "N_LED_A", "K": "0V"},
    "DG1": {"A": "N_ASYM_CLAMP", "K": "VREF"},
    "IC1": {
        "1": "N_DETECTOR_OUT", "2": "N_IC1_2", "3": "N_IC1_6_SENSE",
        "4": "VPLUS", "5": "N_IC1_5", "6": "N_IC1_6_SENSE", "7": "O1",
        "8": "OG", "9": "N_IC1_9", "10": "NREF_AUDIO", "11": "0V",
        "12": "NREF_AUDIO", "13": "N_IC1_13", "14": "OUT14",
    },
    "IC2": {
        "1": "N_IC2_UPPER_OUT", "2": "VPLUS", "3": "N_IC2_UPPER_IN",
        "4": "N_IC2_PIN4_UNRESOLVED", "5": "N_IC2_PIN5_UNRESOLVED",
        "6": "N_IC2_TIED_GATE", "7": "0V", "8": "N_LATCH",
        "9": "N_IC2_PIN9_UNRESOLVED", "10": "N_IC2_PIN10_INPUT",
        "11": "N_IC2_TIMING", "12": "N_IC2_TIED_GATE",
        "13": "N_LATCH", "14": "VPLUS",
    },
}
EXPECTED_POT_TERMINALS = {
    "P1": {"1": "N_IC1_9", "2": "OG", "3": "OG"},
    "P2": {"1": "N_P2_LEFT", "2": "N_P2_WIPER", "3": "N_P2_RIGHT"},
    "P3": {"1": "N_P3_LEFT", "2": "N_P3_WIPER", "3": "N_P3_RIGHT"},
    "P4": {"1": "N_P4_R48", "2": "N_P4_WIPER", "3": "N_P4_C12"},
    "P5": {"1": "N_P5_LEFT", "2": "N_P5_WIPER", "3": "N_DETECTOR_OUT"},
}


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise DuplicateKeyError(f"duplicate object key {key!r}")
        result[key] = value
    return result


def load_json(path: Path, errors: list[str]) -> dict[str, Any]:
    try:
        with path.open("r", encoding="utf-8") as handle:
            value = json.load(handle, object_pairs_hook=_unique_object)
        if not isinstance(value, dict):
            errors.append(f"{path.name}: document root must be an object")
            return {}
        return value
    except (OSError, json.JSONDecodeError, DuplicateKeyError) as exc:
        errors.append(f"{path.name}: cannot load strict JSON: {exc}")
        return {}


def load_bundle(root: Path, errors: list[str]) -> dict[str, dict[str, Any]]:
    return {name: load_json(root / name, errors) for name in DATA_FILES}


def canonical_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        + "\n"
    ).encode("utf-8")


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def quantity_tuple(component: dict[str, Any]) -> tuple[Any, Any] | None:
    value = component.get("value")
    if value is None:
        return None
    if not isinstance(value, dict):
        return (None, None)
    return value.get("decimal"), value.get("unit")


def check_profile_ids_and_formats(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    for name, document in bundle.items():
        if document.get("format") != EXPECTED_FORMATS[name]:
            errors.append(f"{name}: format identifier mismatch")
        field = "documentary_profile_id" if name == "manifest.json" else "profile_id"
        if document.get(field) != PROFILE_ID:
            errors.append(f"{name}: documentary profile ID mismatch")


def check_manifest(bundle: dict[str, dict[str, Any]], root: Path, errors: list[str]) -> None:
    manifest = bundle["manifest.json"]
    packet = manifest.get("source_profile", {}).get("service_packet", {})
    if packet.get("sha256") != P_SM_SHA256 or packet.get("bytes") != P_SM_BYTES:
        errors.append("manifest: exact P-SM hash/byte contract changed")
    if packet.get("pages") != 4:
        errors.append("manifest: P-SM must remain a four-page packet")
    if manifest.get("claim_level") != "documentary_nominal_not_hardware_calibrated":
        errors.append("manifest: claim level must remain documentary nominal")
    if manifest.get("created_from_primary_source_reread") is not True:
        errors.append("manifest: primary-source reread declaration missing")
    if manifest.get("not_a_production_revision") is not True:
        errors.append("manifest: compound drawing set may not claim one production revision")
    if manifest.get("files") != EXPECTED_MANIFEST_FILES:
        errors.append("manifest: package file map changed or is incomplete")
    for role, relative in manifest.get("files", {}).items():
        path = Path(relative)
        if path.is_absolute() or ".." in path.parts:
            errors.append(f"manifest: unsafe path for {role}: {relative}")
        elif not (root / path).is_file():
            errors.append(f"manifest: missing {role} file {relative}")
    selected = manifest.get("selected", {})
    expected_selected = {
        "configuration": "STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY",
        "assumption_set": "all assumptions.json selected_variant fields",
        "op_amp_profile": "OPAMP-4741-FAMILY-GENERIC-V1",
        "r35_profile": "R35-PRESET-SERVICE-NOMINAL-MAXIMUM-V1",
        "supply_profile": "SUPPLY-9V-EFFECTIVE-V1",
        "source_load_profile": "BOUNDARY-STUDIO-GENERIC-V1",
        "input_boundary_profile": "INPUT-BOUNDARY-SERVICE-INTERNAL-V1",
    }
    if selected != expected_selected:
        errors.append("manifest: selected first-oracle state/profile contract changed")
    inventory = manifest.get("inventory_contract", {})
    if inventory != {"service_designators": 111, "populated": 110, "not_used": ["R26"]}:
        errors.append("manifest: inventory contract changed")
    excluded = set(manifest.get("scope_exclusions", []))
    required_exclusions = {
        "M1 SPICE/MNA oracle implementation",
        "realtime DSP",
        "existing NAM",
        "delay",
        "integration",
        "UI",
        "production parameter code",
    }
    if not required_exclusions <= excluded:
        errors.append("manifest: required M0a scope exclusions are incomplete")


def check_evidence(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    evidence = bundle["source-evidence.json"]
    sources = evidence.get("sources", {})
    if set(sources) != {"P-SM", "P-UM", "P-XR4741", "P-HA4741"}:
        errors.append("evidence: registered source ID set changed")
    psm = sources.get("P-SM", {})
    if psm.get("sha256") != P_SM_SHA256 or psm.get("bytes") != P_SM_BYTES:
        errors.append("evidence: exact P-SM identity changed")
    policy = evidence.get("policy", {})
    if policy.get("normative_topology_and_nominal_values") != ["P-SM"]:
        errors.append("evidence: only P-SM may be normative for topology/values")
    if policy.get("comparison_sources_may_resolve_topology") is not False:
        errors.append("evidence: comparison sources may not resolve topology")
    anchors = evidence.get("anchors", [])
    anchor_ids = [item.get("id") for item in anchors if isinstance(item, dict)]
    if len(anchor_ids) != len(set(anchor_ids)):
        errors.append("evidence: duplicate evidence anchor ID")
    required_anchors = {
        "EV-SCHEMATIC-POWER",
        "EV-SCHEMATIC-CONTROL",
        "EV-SCHEMATIC-INPUT",
        "EV-SCHEMATIC-DISTORTION",
        "EV-SCHEMATIC-GAIN-TONE",
        "EV-SCHEMATIC-SUPPRESSOR",
        "EV-SCHEMATIC-OUTPUT",
        "EV-BOM-ALL",
        "EV-LAYOUT-ALL",
        "EV-HA4741-PINOUT",
        "EV-HA4741-ELECTRICAL",
    }
    if not required_anchors <= set(anchor_ids):
        errors.append(f"evidence: missing anchors {sorted(required_anchors - set(anchor_ids))}")
    for anchor in anchors:
        if anchor.get("source") not in sources:
            errors.append(f"evidence: {anchor.get('id')} cites an unknown source")


def check_service(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    service = bundle["service-components.json"]
    if service.get("normative_source") != "P-SM":
        errors.append("service: normative source declaration changed")
    components = service.get("components", [])
    if not isinstance(components, list):
        errors.append("service: components must be an array")
        return
    refs = [item.get("ref") for item in components if isinstance(item, dict)]
    wanted = expected_refs()
    if len(refs) != 111 or len(set(refs)) != 111:
        errors.append(f"service: expected 111 unique designators, found {len(refs)}/{len(set(refs))} unique")
    if set(refs) != set(wanted):
        errors.append(
            "service: designator set mismatch; "
            f"missing={sorted(set(wanted) - set(refs))}, extra={sorted(set(refs) - set(wanted))}"
        )
    by_ref = {item.get("ref"): item for item in components if isinstance(item, dict)}
    populated = [item for item in components if item.get("population") == "populated"]
    if len(populated) != 110:
        errors.append(f"service: expected 110 populated entries, found {len(populated)}")
    for item in components:
        if not isinstance(item.get("normalized_service_text"), str) or not item["normalized_service_text"]:
            errors.append(f"service: {item.get('ref')} lacks normalized service text")
        if item.get("population") not in {"populated", "not_used"}:
            errors.append(f"service: {item.get('ref')} has invalid population")
    r26 = by_ref.get("R26", {})
    if (
        r26.get("population") != "not_used"
        or r26.get("value") is not None
        or r26.get("layout_footprint_depicted") is not True
    ):
        errors.append("service: R26 must remain NOT USED with null value and depicted footprint")
    for index, expected in enumerate(EXPECTED_RESISTORS, 1):
        actual = quantity_tuple(by_ref.get(f"R{index}", {}))
        if actual != expected:
            errors.append(f"service: R{index} expected {expected}, found {actual}")
    for index, expected in enumerate(EXPECTED_CAPACITORS, 1):
        actual = quantity_tuple(by_ref.get(f"C{index}", {}))
        if actual != expected:
            errors.append(f"service: C{index} expected {expected}, found {actual}")
    for ref, expected in EXPECTED_POTS.items():
        item = by_ref.get(ref, {})
        actual_quantity = quantity_tuple(item)
        actual = (*actual_quantity, item.get("service_taper")) if actual_quantity else (None, None, None)
        if actual != expected:
            errors.append(f"service: {ref} expected {expected}, found {actual}")
    for ref, expected in EXPECTED_PARTS.items():
        item = by_ref.get(ref, {})
        actual = item.get("kind"), item.get("part")
        if actual != expected:
            errors.append(f"service: {ref} expected immutable kind/part {expected}, found {actual}")
    if by_ref.get("DG1", {}).get("source_part_text") != "AA/119":
        errors.append("service: DG1 source-form AA/119 must be preserved")
    if by_ref.get("P5", {}).get("service_function_source_form") != "TRESHOLD":
        errors.append("service: P5 source-form TRESHOLD must be preserved")
    r35 = by_ref.get("R35", {})
    if r35.get("adjustable_form") != "two-terminal preset/rheostat; 2.2 MOhm is the service nominal, not a measured setting":
        errors.append("service: R35 adjustable-preset form and undocumented-setting caveat changed")
    if "three-pad layout" not in r35.get("editorial_note", ""):
        errors.append("service: R35 must preserve schematic/layout adjustable-form evidence")
    if by_ref.get("R11", {}).get("power") != {
        "rational": {"numerator": "1", "denominator": "3"}, "unit": "W"
    }:
        errors.append("service: R11 1/3 W rating contract changed")


def expected_terminals(item: dict[str, Any]) -> set[str] | None:
    if item.get("population") == "not_used":
        return set()
    kind = item.get("kind")
    if kind in {"resistor", "capacitor"}:
        return {"1", "2"}
    if kind in {"diode", "led", "germanium_diode"}:
        return {"A", "K"}
    if kind in {"bjt_npn", "bjt_pnp"}:
        return {"C", "B", "E"}
    if kind == "jfet":
        return {"D", "G", "S"}
    if kind == "potentiometer":
        return {"1", "2", "3"}
    if kind in {"quad_op_amp", "cmos_transistor_array"}:
        return {str(index) for index in range(1, 15)}
    return None


def check_contact_pairs(label: str, pairs: Any, terminals: set[str], errors: list[str]) -> None:
    if not isinstance(pairs, list):
        errors.append(f"circuit: {label} contacts must be an array")
        return
    for pair in pairs:
        if not isinstance(pair, list) or len(pair) != 2 or not set(pair) <= terminals:
            errors.append(f"circuit: invalid {label} contact pair {pair}")


def check_circuit(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    service_items = bundle["service-components.json"].get("components", [])
    service = {item.get("ref"): item for item in service_items}
    circuit = bundle["circuit.json"]
    nodes = circuit.get("nodes", {})
    connector_nodes = circuit.get("connector_only_nodes", {})
    if set(nodes) & set(connector_nodes):
        errors.append("circuit: ordinary and connector-only node namespaces overlap")
    declared_nodes = set(nodes) | set(connector_nodes)
    terminals = circuit.get("component_terminals", {})
    if set(terminals) != set(service):
        errors.append(
            "circuit: component mapping differs from service inventory; "
            f"missing={sorted(set(service) - set(terminals))}, extra={sorted(set(terminals) - set(service))}"
        )
    used_nodes: set[str] = set()
    for ref, mapping in terminals.items():
        if not isinstance(mapping, dict):
            errors.append(f"circuit: {ref} terminal map must be an object")
            continue
        expected = expected_terminals(service.get(ref, {}))
        if expected is None:
            errors.append(f"circuit: no terminal signature for {ref}/{service.get(ref, {}).get('kind')}")
            continue
        if set(mapping) != expected:
            errors.append(f"circuit: {ref} terminals expected {sorted(expected)}, found {sorted(mapping)}")
        for terminal, node in mapping.items():
            if not isinstance(node, str) or not node:
                errors.append(f"circuit: {ref}.{terminal} lacks a node")
            else:
                used_nodes.add(node)
        if (
            service.get(ref, {}).get("kind") == "resistor"
            and service.get(ref, {}).get("population") == "populated"
            and mapping.get("1") == mapping.get("2")
        ):
            errors.append(f"circuit: {ref} is accidentally shorted to one node")
    undefined = used_nodes - declared_nodes
    if undefined:
        errors.append(f"circuit: undefined component nodes {sorted(undefined)}")
    referenced_nodes = set(used_nodes)
    for connector, mapping in circuit.get("connectors", {}).items():
        if not isinstance(mapping, dict) or not mapping:
            errors.append(f"circuit: connector {connector} has no terminal map")
            continue
        for terminal, node in mapping.items():
            referenced_nodes.add(node)
            if node not in declared_nodes:
                errors.append(f"circuit: connector {connector}.{terminal} uses undefined node {node}")
    for switch, record in circuit.get("switches", {}).items():
        local = record.get("terminals", {})
        local_names = set(local)
        if not local_names:
            errors.append(f"circuit: switch {switch} has no terminals")
        for terminal, node in local.items():
            referenced_nodes.add(node)
            if node not in declared_nodes:
                errors.append(f"circuit: switch {switch}.{terminal} uses undefined node {node}")
        for state, pairs in record.get("contact_table", {}).items():
            check_contact_pairs(f"{switch}/{state}", pairs, local_names, errors)
        for field in ("normal_contacts", "pressed_contacts"):
            if field in record:
                check_contact_pairs(f"{switch}/{field}", record[field], local_names, errors)
    orphaned = declared_nodes - referenced_nodes
    if orphaned:
        errors.append(f"circuit: declared nodes lack any terminal use {sorted(orphaned)}")

    expected_connectors = {
        "INPUT_JACK": {
            "TIP": "INPUT_TIP",
            "SLEEVE": "0V",
            "RING_BATTERY_NEGATIVE": "BATTERY_NEGATIVE_RING",
        },
        "OUTPUT_JACK": {"TIP": "OUTPUT_HOT", "SLEEVE": "0V"},
        "XLR": {"1": "0V", "2": "OUTPUT_HOT", "3": "NC_XLR_3"},
        "EXTERNAL_POWER": {
            "BATTERY_SPRING_A": "BATTERY_POSITIVE_A",
            "VPLUS_CONTACT": "VPLUS",
            "SENSE_SPRING": "PWR_SENSE",
            "RETURN_CONTACT": "0V",
            "ADAPTER_POSITIVE": "VPLUS",
            "ADAPTER_RETURN": "0V",
        },
        "BATTERY": {
            "POSITIVE": "BATTERY_POSITIVE_A",
            "NEGATIVE": "BATTERY_NEGATIVE_RING",
        },
        "EXTERNAL_BYPASS": {"2R": "EXTERNAL_BYPASS_2R", "1R": "0V"},
    }
    if circuit.get("connectors") != expected_connectors:
        errors.append("circuit: frozen connector terminal map changed")
    if set(circuit.get("switches", {})) != {
        "SW_MODE",
        "SW_BYPASS_LOCAL",
        "SW_BYPASS_REMOTE",
        "SW_INPUT_JACK_BATTERY",
        "SW_POWER_JACK",
    }:
        errors.append("circuit: frozen switch inventory changed")

    exact_bindings = {
        "R2": {"1": "VPLUS", "2": "VREF"},
        "R3": {"1": "VREF", "2": "0V"},
        "C1": {"1": "VREF", "2": "0V"},
        "C3": {"1": "POR", "2": "N_LATCH"},
        "C17": {"1": "OG", "2": "NRETURN"},
        "R13": {"1": "N_INPUT_BIAS", "2": "VREF"},
        "R18": {"1": "NREF_AUDIO", "2": "VREF"},
        "R19": {"1": "N_Q1_DRAIN", "2": "NREF_AUDIO"},
        "R35": {"1": "OUT14", "2": "N_R35_R34"},
        "R36": {"1": "OUT14", "2": "N_CTL2"},
        "R48": {"1": "N_MODE_R48", "2": "N_P4_R48"},
        "C25": {"1": "OUT14", "2": "N_OUTPUT_COUPLED"},
        "R39": {"1": "N_OUTPUT_COUPLED", "2": "0V"},
        "R40": {"1": "N_OUTPUT_COUPLED", "2": "OUTPUT_HOT"},
        "D1": {"A": "BATTERY_POSITIVE_A", "K": "VPLUS"},
    }
    for ref, expected in exact_bindings.items():
        if terminals.get(ref) != expected:
            errors.append(f"circuit: frozen primary binding for {ref} changed")
    for ref, expected in EXPECTED_POT_TERMINALS.items():
        if terminals.get(ref) != expected:
            errors.append(f"circuit: frozen potentiometer relationship for {ref} changed")
    for ref, expected in EXPECTED_ACTIVE_TERMINALS.items():
        if terminals.get(ref) != expected:
            errors.append(f"circuit: frozen active-device/polarity map for {ref} changed")
    if terminals.get("IC1", {}).get("4") != "VPLUS" or terminals.get("IC1", {}).get("11") != "0V":
        errors.append("circuit: IC1 supply pins must remain 4=VPLUS and 11=0V")
    if terminals.get("IC2", {}).get("14") != "VPLUS" or terminals.get("IC2", {}).get("7") != "0V":
        errors.append("circuit: IC2 supply pins must remain 14=VPLUS and 7=0V")
    if terminals.get("IC1", {}).get("10") != "NREF_AUDIO" or terminals.get("IC1", {}).get("12") != "NREF_AUDIO":
        errors.append("circuit: corrected IC1 pin10/pin12 NREF_AUDIO junction missing")
    nref_required = {"IC1.10", "IC1.12", "Q4.B", "Q1.S", "R18.1", "R19.2", "R41.2", "R44.2"}
    actual_nref = {
        f"{ref}.{terminal}"
        for ref, mapping in terminals.items()
        for terminal, node in mapping.items()
        if node == "NREF_AUDIO"
    }
    if not nref_required <= actual_nref:
        errors.append(f"circuit: NREF_AUDIO missing {sorted(nref_required - actual_nref)}")
    unresolved_expected = {
        "D17": {"A": "N_D17_A_UNRESOLVED", "K": "N_D17_K_UNRESOLVED"},
        "IC2": {
            "4": "N_IC2_PIN4_UNRESOLVED",
            "5": "N_IC2_PIN5_UNRESOLVED",
            "9": "N_IC2_PIN9_UNRESOLVED",
        },
    }
    for ref, partial in unresolved_expected.items():
        for terminal, expected_node in partial.items():
            if terminals.get(ref, {}).get(terminal) != expected_node:
                errors.append(f"circuit: {ref}.{terminal} must remain explicitly unresolved")
    sites = circuit.get("known_unresolved_sites", [])
    assumption_ids = {item.get("id") for item in bundle["assumptions.json"].get("assumptions", [])}
    site_entities = {entity for site in sites for entity in site.get("entities", [])}
    if len([site.get("id") for site in sites]) != len({site.get("id") for site in sites}):
        errors.append("circuit: duplicate unresolved-site ID")
    for site in sites:
        if site.get("assumption_id") not in assumption_ids:
            errors.append(f"circuit: {site.get('id')} has unknown assumption")
    for ref, partial in unresolved_expected.items():
        for terminal in partial:
            if f"{ref}.{terminal}" not in site_entities:
                errors.append(f"circuit: {ref}.{terminal} lacks an unresolved-site record")
    for name, record in nodes.items():
        if record.get("class") == "unresolved" and name not in used_nodes:
            errors.append(f"circuit: unresolved node {name} is orphaned")
    for ref, record in circuit.get("orientation_records", {}).items():
        if record.get("assumption_id") and record["assumption_id"] not in assumption_ids:
            errors.append(f"circuit: {ref} orientation cites unknown assumption")

    expected_mode = {
        "Boost": [["P1_COMMON", "P1_BOOST"], ["P2_COMMON", "P2_BOOST"]],
        "Distortion": [["P1_COMMON", "P1_DISTORTION"], ["P2_COMMON", "P2_DISTORTION"]],
    }
    if circuit.get("switches", {}).get("SW_MODE", {}).get("contact_table") != expected_mode:
        errors.append("circuit: selected Boost/Distortion mode contact table changed")
    expected_power = {
        "no_adapter": [["A", "VPLUS"], ["SENSE", "RETURN"]],
        "adapter_inserted": [],
    }
    if circuit.get("switches", {}).get("SW_POWER_JACK", {}).get("contact_table") != expected_power:
        errors.append("circuit: selected dual-NC power-jack contact table changed")


def profile_catalog(bundle: dict[str, dict[str, Any]]) -> dict[str, tuple[str, dict[str, Any]]]:
    profiles = bundle["generic-profiles.json"]
    result: dict[str, tuple[str, dict[str, Any]]] = {}
    for group in ("tapers", "passive_device_profiles", "active_devices", "supply_profiles", "boundary_profiles"):
        for name, record in profiles.get(group, {}).items():
            result[name] = (group, record)
    return result


def classification_index(bundle: dict[str, dict[str, Any]]) -> dict[str, str]:
    result: dict[str, str] = {}
    for class_name, ids in bundle["assumptions.json"].get("variant_classification", {}).items():
        for variant_id in ids:
            result[variant_id] = class_name
    return result


def validate_operation(
    operation: dict[str, Any],
    assumption_id: str,
    variant_id: str,
    bundle: dict[str, dict[str, Any]],
    catalog: dict[str, tuple[str, dict[str, Any]]],
    errors: list[str],
) -> None:
    prefix = f"assumptions:{assumption_id}/{variant_id}"
    operation_type = operation.get("type")
    entity = operation.get("entity")
    if operation_type not in ALLOWED_OPERATIONS:
        errors.append(f"{prefix}: forbidden operation {operation_type}")
        return
    if not isinstance(entity, str) or not entity:
        errors.append(f"{prefix}: operation lacks entity")
    serialized = json.dumps(operation, sort_keys=True).lower()
    forbidden_tokens = ("service-components", "mutate_value", "delete_component", "set_value")
    if any(token in serialized for token in forbidden_tokens):
        errors.append(f"{prefix}: operation may not mutate inventory/value evidence")
    circuit = bundle["circuit.json"]
    if operation_type in {"select_profile", "select_boundary_profile"}:
        profile = operation.get("profile")
        if profile not in catalog:
            errors.append(f"{prefix}: unknown profile {profile}")
        elif operation_type == "select_boundary_profile" and catalog[profile][0] != "boundary_profiles":
            errors.append(f"{prefix}: {profile} is not a boundary profile")
    elif operation_type == "bind_terminal":
        parts = entity.rsplit(".", 1) if isinstance(entity, str) else []
        if len(parts) != 2:
            errors.append(f"{prefix}: bind_terminal entity must be OWNER.TERMINAL")
        else:
            owner, terminal = parts
            mapping = circuit.get("component_terminals", {}).get(owner)
            if mapping is None:
                mapping = circuit.get("connectors", {}).get(owner)
            if mapping is None or terminal not in mapping:
                errors.append(f"{prefix}: bind_terminal target {entity} does not exist")
            valid_nodes = set(circuit.get("nodes", {})) | set(circuit.get("connector_only_nodes", {}))
            if operation.get("node") not in valid_nodes:
                errors.append(f"{prefix}: bind_terminal uses undefined node {operation.get('node')}")
    elif operation_type == "link_nodes":
        node_values = operation.get("nodes")
        if not isinstance(node_values, list) or len(node_values) < 2:
            errors.append(f"{prefix}: link_nodes requires at least two nodes")
    elif operation_type == "set_contact_table":
        if entity not in circuit.get("switches", {}):
            errors.append(f"{prefix}: unknown switch {entity}")
        table = operation.get("table")
        if not isinstance(table, (dict, str)):
            errors.append(f"{prefix}: contact table must be an object or circuit pointer")
    elif operation_type == "set_orientation":
        if not isinstance(operation.get("orientation"), str) or not operation.get("orientation"):
            errors.append(f"{prefix}: set_orientation lacks orientation")
    elif operation_type == "set_device_state":
        states = (
            bundle["generic-profiles.json"]
            .get("active_devices", {})
            .get("JFET-BF245A-DOCUMENTARY-STATIC-V1", {})
            .get("states", {})
        )
        if operation.get("state") not in states:
            errors.append(f"{prefix}: unknown device state {operation.get('state')}")
    elif operation_type == "annotation_only":
        if not isinstance(operation.get("note"), str) or not operation.get("note"):
            errors.append(f"{prefix}: annotation_only lacks a note")


def check_assumptions(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    document = bundle["assumptions.json"]
    assumptions = document.get("assumptions", [])
    ids = [item.get("id") for item in assumptions]
    if len(ids) != len(set(ids)):
        errors.append("assumptions: duplicate assumption ID")
    if set(ids) != REQUIRED_ASSUMPTIONS:
        errors.append(
            "assumptions: required set mismatch; "
            f"missing={sorted(REQUIRED_ASSUMPTIONS - set(ids))}, extra={sorted(set(ids) - REQUIRED_ASSUMPTIONS)}"
        )
    if set(document.get("operation_types", [])) != ALLOWED_OPERATIONS:
        errors.append("assumptions: operation type declaration mismatch")
    if set(document.get("impact_axes", [])) != IMPACT_AXES:
        errors.append("assumptions: impact-axis declaration mismatch")
    if set(document.get("impact_values", [])) != IMPACT_VALUES:
        errors.append("assumptions: impact-value declaration mismatch")
    catalog = profile_catalog(bundle)
    sources = set(bundle["source-evidence.json"].get("sources", {}))
    all_variant_ids: list[str] = []
    for item in assumptions:
        assumption_id = item.get("id")
        prefix = f"assumptions:{assumption_id}"
        for field in ("topic", "expected_electrical_consequence"):
            if not isinstance(item.get(field), str) or not item[field]:
                errors.append(f"{prefix}: missing {field}")
        if not isinstance(item.get("affected"), list) or not item.get("affected"):
            errors.append(f"{prefix}: affected entities must be a non-empty array")
        confidence = item.get("confidence")
        if (
            not isinstance(confidence, dict)
            or set(confidence) != {"level", "note"}
            or confidence.get("level") not in {"high", "medium", "low"}
            or not isinstance(confidence.get("note"), str)
            or not confidence.get("note")
        ):
            errors.append(f"{prefix}: confidence must be a strict level/note object")
        impacts = item.get("impacts")
        if not isinstance(impacts, dict) or set(impacts) != IMPACT_AXES:
            errors.append(f"{prefix}: impact matrix incomplete")
        elif any(value not in IMPACT_VALUES for value in impacts.values()):
            errors.append(f"{prefix}: impact matrix has an invalid value")
        variants = item.get("variants", [])
        local_ids = [variant.get("id") for variant in variants]
        all_variant_ids.extend(local_ids)
        if len(variants) < 2 or len(local_ids) != len(set(local_ids)):
            errors.append(f"{prefix}: needs at least two unique variants")
        if item.get("selected_variant") not in local_ids:
            errors.append(f"{prefix}: selected variant absent")
        for variant in variants:
            variant_id = variant.get("id")
            variant_prefix = f"{prefix}/{variant_id}"
            if not isinstance(variant.get("interpretation"), str) or not variant.get("interpretation"):
                errors.append(f"{variant_prefix}: missing interpretation")
            evidence = variant.get("evidence")
            if not isinstance(evidence, list) or not evidence:
                errors.append(f"{variant_prefix}: evidence must be a non-empty array")
            else:
                for record in evidence:
                    if not isinstance(record, dict) or not isinstance(record.get("anchor"), str) or not record.get("anchor"):
                        errors.append(f"{variant_prefix}: incomplete evidence record")
                        continue
                    if record.get("evidence_status") == "none":
                        if not isinstance(record.get("rationale"), str) or not record.get("rationale"):
                            errors.append(f"{variant_prefix}: no-evidence record lacks rationale")
                        if "source" in record:
                            errors.append(f"{variant_prefix}: no-evidence record may not cite a source")
                    elif record.get("source") not in sources or not isinstance(record.get("support"), str) or not record.get("support"):
                        errors.append(f"{variant_prefix}: evidence source/support is invalid")
            operations = variant.get("operations")
            if not isinstance(operations, list) or not operations:
                errors.append(f"{variant_prefix}: operations must be a non-empty array")
            else:
                for operation in operations:
                    if not isinstance(operation, dict):
                        errors.append(f"{variant_prefix}: operation must be an object")
                    else:
                        validate_operation(operation, assumption_id, variant_id, bundle, catalog, errors)
    if len(all_variant_ids) != len(set(all_variant_ids)):
        errors.append("assumptions: variant IDs must be globally unique")
    classifications = document.get("variant_classification", {})
    if set(classifications) != VARIANT_CLASSES:
        errors.append("assumptions: variant classification keys mismatch")
    classified: list[str] = []
    for class_name, variant_ids in classifications.items():
        if not isinstance(variant_ids, list):
            errors.append(f"assumptions: classification {class_name} must be an array")
        else:
            classified.extend(variant_ids)
    if len(classified) != len(set(classified)):
        errors.append("assumptions: each variant must occur in exactly one classification")
    if set(classified) != set(all_variant_ids):
        errors.append(
            "assumptions: classification coverage mismatch; "
            f"missing={sorted(set(all_variant_ids) - set(classified))}, "
            f"extra={sorted(set(classified) - set(all_variant_ids))}"
        )
    class_by_variant = classification_index(bundle)
    for item in assumptions:
        selected = item.get("selected_variant")
        if class_by_variant.get(selected) in NON_RUNNABLE_CLASSES:
            errors.append(f"assumptions:{item.get('id')}: selected variant {selected} is not runnable")


def taper_value(profile: dict[str, Any], x: float) -> float:
    law = profile.get("law")
    if law == "linear":
        return x
    gamma = float(profile.get("parameters", {}).get("gamma"))
    if law == "power":
        return x**gamma
    if law == "reverse_power":
        return 1.0 - (1.0 - x) ** gamma
    raise ValueError(f"unknown taper law {law}")


def check_profiles(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    profiles = bundle["generic-profiles.json"]
    required_tapers = {
        "TAPER-LIN-IDEAL-V1",
        "TAPER-LOG-10PCT-MID-V1",
        "TAPER-NEGLOG-10PCT-MID-V1",
    }
    if set(profiles.get("tapers", {})) != required_tapers:
        errors.append("profiles: exact LIN/LOG/NEG.LOG profile set missing")
    for name, profile in profiles.get("tapers", {}).items():
        if profile.get("measured") is not False or not profile.get("evidence") or not profile.get("limitations"):
            errors.append(f"profiles: {name} must state measured=false, evidence, and limitations")
        try:
            samples = [taper_value(profile, index / 100.0) for index in range(101)]
            if not math.isclose(samples[0], 0.0, abs_tol=1e-12) or not math.isclose(samples[-1], 1.0, abs_tol=1e-12):
                errors.append(f"profiles: {name} endpoints invalid")
            if any(a > b for a, b in zip(samples, samples[1:])):
                errors.append(f"profiles: {name} is not monotonic")
            if any(value < 0.0 or value > 1.0 for value in samples):
                errors.append(f"profiles: {name} leaves normalized resistance range")
        except (TypeError, ValueError, OverflowError) as exc:
            errors.append(f"profiles: {name} cannot be evaluated: {exc}")
    try:
        log_mid = taper_value(profiles["tapers"]["TAPER-LOG-10PCT-MID-V1"], 0.5)
        neg_mid = taper_value(profiles["tapers"]["TAPER-NEGLOG-10PCT-MID-V1"], 0.5)
        if not math.isclose(log_mid, 0.1, rel_tol=1e-12) or not math.isclose(neg_mid, 0.9, rel_tol=1e-12):
            errors.append("profiles: LOG/NEG.LOG midpoint contract changed")
    except (KeyError, TypeError, ValueError):
        pass
    assignments = profiles.get("pot_assignments", {})
    expected_assignments = {
        "P1": "TAPER-LOG-10PCT-MID-V1",
        "P2": "TAPER-LIN-IDEAL-V1",
        "P3": "TAPER-LIN-IDEAL-V1",
        "P4": "TAPER-NEGLOG-10PCT-MID-V1",
        "P5": "TAPER-LOG-10PCT-MID-V1",
    }
    actual_assignments = {ref: record.get("taper") for ref, record in assignments.items()}
    if actual_assignments != expected_assignments:
        errors.append(f"profiles: pot taper assignments changed: {actual_assignments}")
    for ref, record in assignments.items():
        if set(record.get("terminal_map", {})) != {"1", "2", "3"}:
            errors.append(f"profiles: {ref} terminal map incomplete")

    passive = profiles.get("passive_device_profiles", {})
    expected_passive = {
        "R35-PRESET-SERVICE-NOMINAL-MAXIMUM-V1": ("2.2", "generic_maximum"),
        "R35-PRESET-MIDPOINT-SENSITIVITY-V1": ("1.1", "generic_midpoint_sensitivity"),
    }
    if set(passive) != set(expected_passive):
        errors.append("profiles: exact R35 preset profile set changed")
    for name, (resistance, setting) in expected_passive.items():
        profile = passive.get(name, {})
        if (
            profile.get("measured") is not False
            or profile.get("service_nominal") != {"decimal": "2.2", "unit": "MOhm"}
            or profile.get("selected_effective_resistance") != {"decimal": resistance, "unit": "MOhm"}
            or profile.get("setting") != setting
            or not profile.get("evidence")
            or not profile.get("limitations")
        ):
            errors.append(f"profiles: {name} setting/evidence contract changed")

    active = profiles.get("active_devices", {})
    if set(active) != {
        "OPAMP-4741-FAMILY-GENERIC-V1",
        "OPAMP-IDEAL-SENSITIVITY-V1",
        "JFET-BF245A-DOCUMENTARY-STATIC-V1",
    }:
        errors.append("profiles: active-device profile set changed")
    for name, profile in active.items():
        if profile.get("measured") is not False or not profile.get("evidence"):
            errors.append(f"profiles: {name} must state measured=false and evidence")
    op_amp = active.get("OPAMP-4741-FAMILY-GENERIC-V1", {})
    expected_pinout = {
        "1": "OUT_A", "2": "INVERTING_A", "3": "NONINVERTING_A", "4": "VPLUS",
        "5": "NONINVERTING_B", "6": "INVERTING_B", "7": "OUT_B", "8": "OUT_C",
        "9": "INVERTING_C", "10": "NONINVERTING_C", "11": "VMINUS",
        "12": "NONINVERTING_D", "13": "INVERTING_D", "14": "OUT_D",
    }
    if op_amp.get("pinout") != expected_pinout:
        errors.append("profiles: generic 4741 pinout changed")
    if op_amp.get("measured") is not False or len(op_amp.get("not_claimed", [])) < 4:
        errors.append("profiles: generic 4741 must disclaim fitted maker, GR1, unit data, and pedal operating point")
    expected_4741 = {
        "unity_gain_bandwidth_typical": ("3.5", "MHz"),
        "open_loop_gain_typical": ("50000", "V/V"),
        "input_offset_voltage_typical": ("0.5", "mV"),
        "input_bias_current_typical": ("60", "nA"),
        "input_voltage_noise_density_typical": ("9", "nV/sqrtHz"),
        "output_resistance_typical": ("300", "Ohm"),
        "supply_current_typical": ("4.5", "mA"),
    }
    parameters = op_amp.get("parameters", {})
    for name, expected in expected_4741.items():
        parameter = parameters.get(name, {})
        actual = parameter.get("decimal"), parameter.get("unit")
        if actual != expected or not parameter.get("condition") or not parameter.get("evidence"):
            errors.append(f"profiles: condition-tagged 4741 parameter {name} changed/incomplete")
    jfet_states = active.get("JFET-BF245A-DOCUMENTARY-STATIC-V1", {}).get("states", {})
    if set(jfet_states) != {"channel_off", "channel_on", "ideal_open", "dynamic_suppressor_controlled"}:
        errors.append("profiles: JFET documentary state set changed")
    for group in ("supply_profiles", "boundary_profiles"):
        for name, profile in profiles.get(group, {}).items():
            if profile.get("measured") is not False or not profile.get("evidence") or not profile.get("limitations"):
                errors.append(f"profiles: {name} must state measured=false, evidence, and limitations")
    supplies = profiles.get("supply_profiles", {})
    if set(supplies) != {
        "SUPPLY-9V-EFFECTIVE-V1",
        "SUPPLY-9V-ADAPTER-DIRECT-V1",
        "SUPPLY-9V-BATTERY-SWITCHED-V1",
        "SUPPLY-18V-EFFECTIVE-SENSITIVITY-V1",
    }:
        errors.append("profiles: named supply profile set changed")
    selected_supplies = [name for name, profile in supplies.items() if profile.get("selected") is True]
    if selected_supplies != ["SUPPLY-9V-EFFECTIVE-V1"]:
        errors.append("profiles: exactly the effective 9 V supply must be selected")
    effective = supplies.get("SUPPLY-9V-EFFECTIVE-V1", {})
    if effective.get("source_node") != "VPLUS" or effective.get("return_node") != "0V":
        errors.append("profiles: first-oracle supply must source VPLUS relative to 0V")
    if "solve R2/R3/C1" not in effective.get("vref_rule", ""):
        errors.append("profiles: VREF must remain a solved finite-impedance network")
    manual = profiles.get("boundary_profiles", {}).get("INPUT-BOUNDARY-MANUAL-SENSITIVITY-V1", {})
    if manual.get("selected") is not False or "never mutates" not in manual.get("interpretation", ""):
        errors.append("profiles: manual 3-3.3 MOhm claim must be non-default and may not replace R13")


def normalize_closures(closures: Any) -> list[list[str]]:
    return sorted([sorted(pair) for pair in closures]) if isinstance(closures, list) else []


def configuration_index(configs: dict[str, Any]) -> dict[str, dict[str, Any]]:
    result = {item.get("id"): item for item in configs.get("truth_table", [])}
    result.update({item.get("id"): item for item in configs.get("additional_named_configurations", [])})
    return result


def resolve_configuration(configs: dict[str, Any], config_id: str) -> dict[str, Any]:
    index = configuration_index(configs)
    if config_id not in index:
        raise MaterializationError(f"unknown configuration {config_id}")
    visiting: set[str] = set()

    def resolve(current_id: str) -> dict[str, Any]:
        if current_id in visiting:
            raise MaterializationError(f"configuration inheritance cycle at {current_id}")
        visiting.add(current_id)
        item = index[current_id]
        if "base" in item:
            base_id = item.get("base")
            if base_id not in index:
                raise MaterializationError(f"configuration {current_id} has unknown base {base_id}")
            resolved = resolve(base_id)
            resolved.update(copy.deepcopy(item.get("override", {})))
            resolved["id"] = current_id
            resolved["derived_from"] = base_id
            resolved["selected_for_first_oracle"] = False
            if "purpose" in item:
                resolved["purpose"] = item["purpose"]
        else:
            resolved = copy.deepcopy(item)
        visiting.remove(current_id)
        return resolved

    return resolve(config_id)


def check_configurations(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    configs = bundle["configurations.json"]
    table = configs.get("truth_table", [])
    ids = [row.get("id") for row in table]
    if len(ids) != len(set(ids)):
        errors.append("configurations: duplicate truth-table ID")
    combinations = {(row.get("mode"), row.get("bypass")) for row in table}
    expected = {(mode, bypass) for mode in ("Boost", "Distortion") for bypass in ("engaged", "bypass")}
    if len(table) != 4 or combinations != expected:
        errors.append("configurations: truth table must cover exactly Boost/Distortion x engaged/bypass")
    selected = [row for row in table if row.get("selected_for_first_oracle") is True]
    if len(selected) != 1 or selected[0].get("id") != "STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY":
        errors.append("configurations: exactly one engaged-Boost first oracle must be selected")
    elif (
        selected[0].get("Q1_state") != "channel_off_branch_retained"
        or selected[0].get("Q2_state") != "channel_off_full_level"
        or selected[0].get("supply") != "effective_9V"
    ):
        errors.append("configurations: first-oracle static Q1/Q2/supply contract changed")
    mode_contacts = configs.get("mode_contacts", {})
    circuit_mode = bundle["circuit.json"].get("switches", {}).get("SW_MODE", {}).get("contact_table", {})
    for row in table:
        mode = row.get("mode")
        expected_local = circuit_mode.get(mode, [])
        if normalize_closures(row.get("mode_closures")) != normalize_closures(expected_local):
            errors.append(f"configurations: {row.get('id')} mode closures disagree with circuit")
        qualified = [[f"SW_MODE.{item}" for item in pair] for pair in expected_local]
        if normalize_closures(mode_contacts.get(mode, {}).get("closures")) != normalize_closures(qualified):
            errors.append(f"configurations: {mode} contact narrative disagrees with circuit")
        if row.get("supply") not in configs.get("supply_bindings", {}):
            errors.append(f"configurations: {row.get('id')} selects unknown supply binding")
    if "remains physically connected" not in mode_contacts.get("Boost", {}).get("distortion_branch", ""):
        errors.append("configurations: Boost must retain the Distortion-branch loading")
    supplies = bundle["generic-profiles.json"].get("supply_profiles", {})
    switches = bundle["circuit.json"].get("switches", {})
    for binding_id, binding in configs.get("supply_bindings", {}).items():
        if binding.get("profile") not in supplies:
            errors.append(f"configurations: {binding_id} uses unknown supply profile")
        for switch, state in binding.get("switch_states", {}).items():
            if state not in switches.get(switch, {}).get("contact_table", {}):
                errors.append(f"configurations: {binding_id} uses unknown {switch} state {state}")
    all_ids = set(ids)
    additional_ids: set[str] = set()
    for item in configs.get("additional_named_configurations", []):
        item_id = item.get("id")
        if item_id in all_ids or item_id in additional_ids:
            errors.append(f"configurations: duplicate configuration ID {item_id}")
        additional_ids.add(item_id)
        if item.get("base") not in all_ids:
            errors.append(f"configurations: {item_id} has unknown/non-base truth-table configuration")
        if not isinstance(item.get("override"), dict) or not item.get("override"):
            errors.append(f"configurations: {item_id} requires an override")
        try:
            resolve_configuration(configs, item_id)
        except MaterializationError as exc:
            errors.append(f"configurations: {exc}")
    if "STATE-ENGAGED-BOOST-DYNAMIC-SUPPRESSOR" not in additional_ids:
        errors.append("configurations: dynamic-suppressor Boost configuration missing")


def expand_designator_token(token: str) -> list[str]:
    if not isinstance(token, str) or not token:
        raise ValueError("empty/non-string designator token")
    if "-" not in token:
        return [token]
    first, last = token.split("-", 1)
    prefix_a = "".join(character for character in first if not character.isdigit())
    prefix_b = "".join(character for character in last if not character.isdigit())
    number_a = first[len(prefix_a):]
    number_b = last[len(prefix_b):]
    if not prefix_a or prefix_a != prefix_b or not number_a.isdigit() or not number_b.isdigit():
        raise ValueError(f"invalid designator range {token}")
    start, stop = int(number_a), int(number_b)
    if start > stop:
        raise ValueError(f"descending designator range {token}")
    return [f"{prefix_a}{number}" for number in range(start, stop + 1)]


def check_cross_check(bundle: dict[str, dict[str, Any]], errors: list[str]) -> None:
    cross = bundle["documentary-cross-check.json"]
    if set(cross.get("status_values", [])) != DOCUMENTARY_STATUSES:
        errors.append("cross-check: documentary status vocabulary mismatch")
    if set(cross.get("designator_dimensions", {})) != {"bom", "schematic", "layout_artwork"}:
        errors.append("cross-check: source dimensions must be BOM/schematic/layout_artwork")
    assumptions = {item.get("id") for item in bundle["assumptions.json"].get("assumptions", [])}
    anchor_ids = {item.get("id") for item in bundle["source-evidence.json"].get("anchors", [])}
    expected = set(expected_refs())
    observations: dict[str, dict[str, str]] = {
        source: {} for source in ("bom", "schematic", "layout_artwork")
    }
    for index, group in enumerate(cross.get("designator_observation_groups", [])):
        source = group.get("source")
        status = group.get("status")
        label = f"cross-check: observation group {index}"
        if source not in observations:
            errors.append(f"{label} has invalid source {source}")
            continue
        if status not in DOCUMENTARY_STATUSES:
            errors.append(f"{label} has invalid status {status}")
        evidence_refs: list[str] = []
        if "evidence_anchor" in group:
            evidence_refs.append(group["evidence_anchor"])
        evidence_refs.extend(group.get("evidence_anchors", []))
        if not evidence_refs or any(anchor not in anchor_ids for anchor in evidence_refs):
            errors.append(f"{label} lacks valid source-evidence anchor(s)")
        if status in UNCERTAIN_STATUSES and group.get("assumption_id") not in assumptions:
            errors.append(f"{label} uncertainty lacks a valid assumption")
        for token in group.get("refs", []):
            try:
                refs = expand_designator_token(token)
            except ValueError as exc:
                errors.append(f"{label}: {exc}")
                continue
            for ref in refs:
                if ref not in expected:
                    errors.append(f"{label} includes unknown designator {ref}")
                if ref in observations[source]:
                    errors.append(f"cross-check: {ref}/{source} has duplicate observation coverage")
                observations[source][ref] = status
    for source, records in observations.items():
        missing = expected - set(records)
        extra = set(records) - expected
        if missing or extra:
            errors.append(f"cross-check: {source} observation coverage mismatch; missing={sorted(missing)}, extra={sorted(extra)}")

    checks = cross.get("material_checks", [])
    check_ids = [check.get("id") for check in checks]
    if len(check_ids) != len(set(check_ids)):
        errors.append("cross-check: duplicate material-check ID")
    if set(check_ids) != REQUIRED_MATERIAL_CHECKS:
        errors.append(
            "cross-check: material-check set mismatch; "
            f"missing={sorted(REQUIRED_MATERIAL_CHECKS - set(check_ids))}, "
            f"extra={sorted(set(check_ids) - REQUIRED_MATERIAL_CHECKS)}"
        )
    for check in checks:
        prefix = f"cross-check:{check.get('id')}"
        if not isinstance(check.get("subject"), str) or not check.get("subject"):
            errors.append(f"{prefix}: missing subject")
        statuses = [check.get(source) for source in ("schematic", "bom", "layout_artwork")]
        if any(status not in DOCUMENTARY_STATUSES for status in statuses):
            errors.append(f"{prefix}: invalid/missing source status")
        supporting_status = check.get("supporting_status")
        uncertainty = any(status in UNCERTAIN_STATUSES for status in statuses)
        uncertainty = uncertainty or supporting_status in UNCERTAIN_STATUSES
        if uncertainty and check.get("assumption_id") not in assumptions:
            errors.append(f"{prefix}: uncertainty lacks a valid assumption")
        if "supporting_source" in check and check["supporting_source"] not in bundle["source-evidence.json"].get("sources", {}):
            errors.append(f"{prefix}: unknown supporting source")
    reviews = cross.get("authoring_reviews", [])
    if not reviews or any(review.get("is_independent_freeze_audit") is not False for review in reviews):
        errors.append("cross-check: authoring review must exist and may not claim independent audit")
    if not isinstance(cross.get("independent_freeze_audits"), list):
        errors.append("cross-check: independent_freeze_audits must be an array")


class UnionFind:
    def __init__(self, nodes: set[str]):
        self.parent = {node: node for node in nodes}

    def find(self, node: str) -> str:
        if node not in self.parent:
            self.parent[node] = node
        root = node
        while self.parent[root] != root:
            root = self.parent[root]
        while self.parent[node] != node:
            parent = self.parent[node]
            self.parent[node] = root
            node = parent
        return root

    def union(self, first: str, second: str) -> None:
        root_a, root_b = self.find(first), self.find(second)
        if root_a == root_b:
            return
        canonical, other = sorted((root_a, root_b))
        self.parent[other] = canonical

    def classes(self) -> dict[str, list[str]]:
        result: dict[str, list[str]] = {}
        for node in sorted(self.parent):
            result.setdefault(self.find(node), []).append(node)
        return {key: value for key, value in sorted(result.items())}


def _pointer_contact_table(pointer: str, entity: str, base_switches: dict[str, Any]) -> dict[str, Any]:
    expected = f"circuit.json#switches/{entity}/contact_table"
    if pointer != expected:
        raise MaterializationError(f"unsupported contact-table pointer {pointer}")
    return copy.deepcopy(base_switches[entity]["contact_table"])


def _apply_orientation(entity: str, orientation: str, terminals: dict[str, dict[str, str]]) -> None:
    if orientation in {"as_circuit_json", "reverse_normalized_shaft"}:
        return
    if orientation == "swap_1_3" and entity in terminals:
        terminals[entity]["1"], terminals[entity]["3"] = terminals[entity]["3"], terminals[entity]["1"]
        return
    if orientation == "swap_refs" and entity == "D14/D15":
        terminals["D14"], terminals["D15"] = terminals["D15"], terminals["D14"]
        return
    if orientation == "swap_D_S" and entity == "Q1/Q2":
        for ref in ("Q1", "Q2"):
            terminals[ref]["D"], terminals[ref]["S"] = terminals[ref]["S"], terminals[ref]["D"]
        return
    raise MaterializationError(f"unsupported executable orientation {entity}={orientation}")


def select_variants(
    bundle: dict[str, dict[str, Any]],
    overrides: dict[str, str] | None = None,
) -> tuple[dict[str, str], dict[str, dict[str, Any]], dict[str, str]]:
    assumptions = bundle["assumptions.json"].get("assumptions", [])
    assumptions_by_id = {item["id"]: item for item in assumptions}
    selected = {item["id"]: item["selected_variant"] for item in assumptions}
    for assumption_id, variant_id in (overrides or {}).items():
        if assumption_id not in assumptions_by_id:
            raise MaterializationError(f"unknown assumption override {assumption_id}")
        allowed = {variant["id"] for variant in assumptions_by_id[assumption_id]["variants"]}
        if variant_id not in allowed:
            raise MaterializationError(f"{variant_id} is not a variant of {assumption_id}")
        selected[assumption_id] = variant_id
    classes = classification_index(bundle)
    selected_variants: dict[str, dict[str, Any]] = {}
    selected_classes: dict[str, str] = {}
    for assumption_id, variant_id in selected.items():
        class_name = classes.get(variant_id)
        if class_name in NON_RUNNABLE_CLASSES:
            raise MaterializationError(
                f"{assumption_id}={variant_id} is {class_name} and cannot be materialized"
            )
        if class_name not in VARIANT_CLASSES:
            raise MaterializationError(f"{variant_id} lacks a valid classification")
        selected_classes[assumption_id] = class_name
        selected_variants[assumption_id] = next(
            variant for variant in assumptions_by_id[assumption_id]["variants"] if variant["id"] == variant_id
        )
    return selected, selected_variants, selected_classes


DEVICE_STATE_MAP = {
    "channel_off_branch_retained": "channel_off",
    "channel_off_full_level": "channel_off",
    "dynamic_suppressor_controlled": "dynamic_suppressor_controlled",
    "channel_on": "channel_on",
    "channel_on_bypass_override": "channel_on",
}


def materialized_payload(
    bundle: dict[str, dict[str, Any]],
    assumption_overrides: dict[str, str] | None = None,
    configuration_id: str | None = None,
) -> dict[str, Any]:
    selected, selected_variants, selected_classes = select_variants(bundle, assumption_overrides)
    manifest = bundle["manifest.json"]
    configs = bundle["configurations.json"]
    config_id = configuration_id or manifest["selected"]["configuration"]
    configuration = resolve_configuration(configs, config_id)
    circuit = bundle["circuit.json"]
    base_switches = circuit["switches"]
    component_terminals = copy.deepcopy(circuit["component_terminals"])
    connectors = copy.deepcopy(circuit["connectors"])
    switches = copy.deepcopy(base_switches)
    orientations: dict[str, str] = {}
    annotations: list[dict[str, str]] = []
    explicit_links: list[list[str]] = []
    selected_profiles = {
        "IC1": manifest["selected"]["op_amp_profile"],
        "R35": manifest["selected"]["r35_profile"],
        "POWER": configs["supply_bindings"][configuration["supply"]]["profile"],
        "AUDIO_IO": manifest["selected"]["source_load_profile"],
        "INPUT": manifest["selected"]["input_boundary_profile"],
    }
    device_states = {
        "Q1": DEVICE_STATE_MAP.get(configuration.get("Q1_state"), configuration.get("Q1_state")),
        "Q2": DEVICE_STATE_MAP.get(configuration.get("Q2_state"), configuration.get("Q2_state")),
    }
    applied_operations: list[dict[str, Any]] = []

    for assumption in bundle["assumptions.json"]["assumptions"]:
        assumption_id = assumption["id"]
        variant = selected_variants[assumption_id]
        for operation in variant["operations"]:
            operation = copy.deepcopy(operation)
            operation["assumption_id"] = assumption_id
            operation["variant_id"] = variant["id"]
            applied_operations.append(operation)
            operation_type = operation["type"]
            entity = operation["entity"]
            if operation_type == "bind_terminal":
                owner, terminal = entity.rsplit(".", 1)
                if owner in component_terminals:
                    component_terminals[owner][terminal] = operation["node"]
                elif owner in connectors:
                    connectors[owner][terminal] = operation["node"]
                else:
                    raise MaterializationError(f"cannot bind unknown owner {owner}")
            elif operation_type == "link_nodes":
                nodes = operation["nodes"]
                explicit_links.extend([[nodes[0], node] for node in nodes[1:]])
            elif operation_type == "set_contact_table":
                table = operation["table"]
                if isinstance(table, str):
                    table = _pointer_contact_table(table, entity, base_switches)
                switches[entity]["contact_table"] = copy.deepcopy(table)
            elif operation_type in {"select_profile", "select_boundary_profile"}:
                selected_profiles[entity] = operation["profile"]
            elif operation_type == "set_device_state":
                device_states[entity] = operation["state"]
            elif operation_type == "set_orientation":
                orientations[entity] = operation["orientation"]
                _apply_orientation(entity, operation["orientation"], component_terminals)
            elif operation_type == "annotation_only":
                annotations.append({
                    "assumption_id": assumption_id,
                    "variant_id": variant["id"],
                    "entity": entity,
                    "note": operation["note"],
                })

    catalog = profile_catalog(bundle)
    # A named configuration owns its supply unless the caller explicitly chose
    # an A-SUPPLY-VREF variant. This makes the named 18 V configuration real
    # while preserving command-line assumption overrides as highest priority.
    if not assumption_overrides or "A-SUPPLY-VREF" not in assumption_overrides:
        selected_profiles["POWER"] = configs["supply_bindings"][configuration["supply"]]["profile"]
    for entity, profile_id in selected_profiles.items():
        if profile_id not in catalog:
            raise MaterializationError(f"selected {entity} profile {profile_id} does not exist")
    power_profile_id = selected_profiles["POWER"]
    power_profile = catalog[power_profile_id][1]
    active_switch_states: dict[str, str] = {"SW_MODE": configuration["mode"]}
    active_switch_states.update(power_profile.get("switch_states", {}))
    if not power_profile.get("switch_states"):
        active_switch_states.update(
            configs["supply_bindings"].get(configuration["supply"], {}).get("switch_states", {})
        )

    all_nodes = set(circuit["nodes"]) | set(circuit.get("connector_only_nodes", {}))
    for mapping in component_terminals.values():
        all_nodes.update(mapping.values())
    for mapping in connectors.values():
        all_nodes.update(mapping.values())
    for switch in switches.values():
        all_nodes.update(switch.get("terminals", {}).values())
    union = UnionFind(all_nodes)
    for first, second in explicit_links:
        if first not in all_nodes or second not in all_nodes:
            raise MaterializationError(f"link_nodes uses undefined nodes {first}, {second}")
        union.union(first, second)
    active_closures: list[dict[str, Any]] = []
    for switch_name, state in sorted(active_switch_states.items()):
        switch = switches.get(switch_name)
        if switch is None or state not in switch.get("contact_table", {}):
            raise MaterializationError(f"unknown active switch state {switch_name}={state}")
        for pair in switch["contact_table"][state]:
            first, second = pair
            node_a = switch["terminals"][first]
            node_b = switch["terminals"][second]
            union.union(node_a, node_b)
            active_closures.append({
                "switch": switch_name,
                "state": state,
                "terminals": [first, second],
                "documentary_nodes": [node_a, node_b],
            })
    resolved_component_terminals = {
        ref: {terminal: union.find(node) for terminal, node in mapping.items()}
        for ref, mapping in component_terminals.items()
    }
    resolved_connectors = {
        connector: {terminal: union.find(node) for terminal, node in mapping.items()}
        for connector, mapping in connectors.items()
    }
    service = {item["ref"]: item for item in bundle["service-components.json"]["components"]}
    for ref, mapping in resolved_component_terminals.items():
        if (
            service[ref].get("kind") == "resistor"
            and service[ref].get("population") == "populated"
            and mapping.get("1") == mapping.get("2")
        ):
            raise MaterializationError(f"selected state accidentally shorts resistor {ref}")
    if union.find("VREF") in {union.find("0V"), union.find("VPLUS")}:
        raise MaterializationError("selected state collapses finite-impedance VREF into a rail")
    dynamic = device_states.get("Q2") == "dynamic_suppressor_controlled"
    unresolved_control = bool(circuit.get("known_unresolved_sites"))
    selected_profile_definitions = {
        profile_id: {
            "group": catalog[profile_id][0],
            "definition": catalog[profile_id][1],
        }
        for profile_id in sorted(set(selected_profiles.values()))
    }
    return {
        "format": "tc-bld-m0a-resolved-handoff-v1",
        "materializer_version": 1,
        "profile_id": PROFILE_ID,
        "source_sha256": P_SM_SHA256,
        "claim_level": "documentary_nominal_not_hardware_calibrated",
        "configuration": configuration,
        "selected_assumptions": {
            assumption_id: {
                "variant": variant_id,
                "classification": selected_classes[assumption_id],
            }
            for assumption_id, variant_id in sorted(selected.items())
        },
        "applied_operations": applied_operations,
        "selected_profiles": dict(sorted(selected_profiles.items())),
        "selected_profile_definitions": selected_profile_definitions,
        "device_states": dict(sorted(device_states.items())),
        "orientation_overrides": dict(sorted(orientations.items())),
        "annotations": annotations,
        "service_components": bundle["service-components.json"]["components"],
        "nodes": circuit["nodes"],
        "connector_only_nodes": circuit.get("connector_only_nodes", {}),
        "documentary_component_terminals": component_terminals,
        "component_terminals": resolved_component_terminals,
        "documentary_connectors": connectors,
        "connectors": resolved_connectors,
        "switches": switches,
        "active_switch_states": dict(sorted(active_switch_states.items())),
        "active_switch_closures": active_closures,
        "node_equivalence_classes": union.classes(),
        "documentary_orientation_records": circuit.get("orientation_records", {}),
        "unresolved_sites": circuit.get("known_unresolved_sites", []),
        "oracle_readiness": {
            "static_engaged_boost_first_oracle": config_id == "STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY" and not dynamic,
            "dynamic_latch_or_suppressor_runnable": not (dynamic and unresolved_control),
            "note": "D17 endpoints and IC2 pins 4/5/9 block a complete dynamic control oracle; visible IC2 pin 11 is bound to R4/C4 and the named static first oracle remains runnable.",
        },
    }


def check_materializable_variants(
    bundle: dict[str, dict[str, Any]],
    errors: list[str],
) -> None:
    """Prove every declared runnable variant is executable, not just defaults."""

    classes = classification_index(bundle)
    for assumption in bundle["assumptions.json"].get("assumptions", []):
        assumption_id = assumption.get("id")
        for variant in assumption.get("variants", []):
            variant_id = variant.get("id")
            if classes.get(variant_id) in NON_RUNNABLE_CLASSES:
                continue
            try:
                materialized_payload(bundle, {assumption_id: variant_id})
            except (KeyError, TypeError, MaterializationError) as exc:
                errors.append(
                    f"materializer: runnable {assumption_id}={variant_id} failed: {exc}"
                )


def normalized_cross_check(cross: dict[str, Any]) -> dict[str, Any]:
    normalized = copy.deepcopy(cross)
    normalized["independent_freeze_audits"] = []
    normalized["freeze_gate_status"] = "pending_fresh_session_audit"
    return normalized


def normative_bundle_sha256(bundle: dict[str, dict[str, Any]]) -> str:
    normalized = copy.deepcopy(bundle)
    normalized["documentary-cross-check.json"] = normalized_cross_check(normalized["documentary-cross-check.json"])
    return sha256_bytes(canonical_bytes(normalized))


def audited_file_sha256(root: Path, bundle: dict[str, dict[str, Any]]) -> dict[str, str]:
    relative_files = {"manifest.json", *EXPECTED_MANIFEST_FILES.values()}
    result: dict[str, str] = {}
    for relative in sorted(relative_files):
        path = root / relative
        if relative == "documentary-cross-check.json":
            content = canonical_bytes(normalized_cross_check(bundle[relative]))
        else:
            content = path.read_bytes()
        result[relative] = sha256_bytes(content)
    return result


def artifact_digests(
    root: Path,
    bundle: dict[str, dict[str, Any]],
    payload: dict[str, Any] | None = None,
) -> dict[str, Any]:
    resolved = payload if payload is not None else materialized_payload(bundle)
    file_hashes = audited_file_sha256(root, bundle)
    return {
        "normative_bundle_sha256": normative_bundle_sha256(bundle),
        "resolved_sha256": sha256_bytes(canonical_bytes(resolved)),
        "validator_sha256": file_hashes["tools/validate_m0a.py"],
        "audited_file_sha256": file_hashes,
    }


def build_audit_template(
    root: Path,
    bundle: dict[str, dict[str, Any]],
    payload: dict[str, Any] | None = None,
) -> dict[str, Any]:
    digests = artifact_digests(root, bundle, payload)
    return {
        "id": "AUDIT-REPLACE-ME",
        "auditor": "REPLACE-WITH-AUDITOR-IDENTITY",
        "session_id": "REPLACE-WITH-DISTINCT-SESSION-ID",
        "audited_at": "REPLACE-WITH-ISO-8601-TIMESTAMP",
        "distinct_session": True,
        "result": "pass",
        "profile_id": PROFILE_ID,
        "source_sha256": P_SM_SHA256,
        **digests,
        "coverage": {item: "pass" for item in AUDIT_COVERAGE},
        "hash_basis": (
            "documentary-cross-check.json is hashed canonically with independent_freeze_audits=[] "
            "and freeze_gate_status=pending_fresh_session_audit to avoid self-reference"
        ),
    }


def check_freeze_audits(
    root: Path,
    bundle: dict[str, dict[str, Any]],
    payload: dict[str, Any],
    errors: list[str],
) -> None:
    cross = bundle["documentary-cross-check.json"]
    expected = build_audit_template(root, bundle, payload)
    valid = False
    reasons: list[str] = []
    for audit in cross.get("independent_freeze_audits", []):
        audit_id = audit.get("id", "<missing-id>")
        local: list[str] = []
        for field in ("id", "auditor", "session_id", "audited_at"):
            value = audit.get(field)
            if not isinstance(value, str) or not value or "REPLACE" in value:
                local.append(f"invalid {field}")
        if audit.get("distinct_session") is not True:
            local.append("distinct_session is not true")
        if audit.get("result") != "pass":
            local.append("result is not pass")
        for field in (
            "profile_id",
            "source_sha256",
            "normative_bundle_sha256",
            "resolved_sha256",
            "validator_sha256",
            "audited_file_sha256",
            "coverage",
            "hash_basis",
        ):
            if audit.get(field) != expected[field]:
                local.append(f"stale/mismatched {field}")
        if not local:
            valid = True
            break
        reasons.append(f"{audit_id}: {', '.join(local)}")
    if not valid:
        suffix = f" ({'; '.join(reasons)})" if reasons else ""
        errors.append(
            "freeze gate: no passing distinct fresh-session audit bound to the exact "
            f"source/package/resolved/validator/file digests{suffix}"
        )
    elif cross.get("freeze_gate_status") != "passed_fresh_session_audit":
        errors.append("freeze gate: set freeze_gate_status to passed_fresh_session_audit with the audit record")


def validate(root: Path, gate: str = "authoring") -> tuple[list[str], dict[str, dict[str, Any]]]:
    errors: list[str] = []
    bundle = load_bundle(root, errors)
    if errors:
        return errors, bundle
    check_profile_ids_and_formats(bundle, errors)
    check_manifest(bundle, root, errors)
    check_evidence(bundle, errors)
    check_service(bundle, errors)
    check_circuit(bundle, errors)
    check_assumptions(bundle, errors)
    check_profiles(bundle, errors)
    check_configurations(bundle, errors)
    check_cross_check(bundle, errors)
    payload: dict[str, Any] | None = None
    if not errors:
        check_materializable_variants(bundle, errors)
    if not errors:
        try:
            payload = materialized_payload(bundle)
        except (KeyError, TypeError, MaterializationError) as exc:
            errors.append(f"materializer: {exc}")
    if gate == "freeze" and not errors and payload is not None:
        try:
            check_freeze_audits(root, bundle, payload, errors)
        except OSError as exc:
            errors.append(f"freeze gate: cannot hash package: {exc}")
    return errors, bundle


def parse_selections(values: list[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for value in values:
        if value.count("=") != 1:
            raise MaterializationError(f"selection must be ASSUMPTION_ID=VARIANT_ID: {value}")
        assumption_id, variant_id = value.split("=", 1)
        if not assumption_id or not variant_id:
            raise MaterializationError(f"selection must be ASSUMPTION_ID=VARIANT_ID: {value}")
        if assumption_id in result:
            raise MaterializationError(f"duplicate selection override for {assumption_id}")
        result[assumption_id] = variant_id
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gate", choices=("authoring", "freeze"), default="authoring")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument(
        "--select",
        action="append",
        default=[],
        metavar="ASSUMPTION=VARIANT",
        help="materialize one typed alternative (repeatable)",
    )
    parser.add_argument("--configuration", help="materialize a named truth-table or derived configuration")
    parser.add_argument(
        "--emit-resolved",
        type=Path,
        help="write canonical resolved M1 handoff JSON (do not check generated output into M0a)",
    )
    parser.add_argument(
        "--print-audit-template",
        action="store_true",
        help="print the exact fresh-session audit record template for the default first oracle",
    )
    args = parser.parse_args(argv)
    root = args.root.resolve()
    errors, bundle = validate(root, args.gate)
    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1
    try:
        overrides = parse_selections(args.select)
        if args.print_audit_template and (overrides or args.configuration):
            raise MaterializationError("audit template is defined only for the default selected first-oracle state")
        payload = materialized_payload(bundle, overrides, args.configuration)
        encoded = canonical_bytes(payload)
        digests = artifact_digests(root, bundle, payload)
        if args.emit_resolved:
            if not args.emit_resolved.parent.is_dir():
                raise MaterializationError(f"output parent does not exist: {args.emit_resolved.parent}")
            args.emit_resolved.write_bytes(encoded)
        if args.print_audit_template:
            print(json.dumps(build_audit_template(root, bundle, payload), indent=2, sort_keys=True))
            return 0
    except (KeyError, OSError, TypeError, MaterializationError) as exc:
        print(f"ERROR: materialization failed: {exc}", file=sys.stderr)
        return 1
    print(f"PASS {args.gate}: {PROFILE_ID}")
    print(f"normative_bundle_sha256={digests['normative_bundle_sha256']}")
    print(f"resolved_sha256={digests['resolved_sha256']}")
    print(f"validator_sha256={digests['validator_sha256']}")
    if args.gate == "authoring":
        print("independent_freeze_audit=pending (expected for M0a authoring handoff)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
