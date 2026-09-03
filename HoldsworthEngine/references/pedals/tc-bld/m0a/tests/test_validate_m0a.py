#!/usr/bin/env python3
"""Regression tests for the standalone TC BLD M0a validator/materializer."""

import copy
import hashlib
import importlib.util
import json
import shutil
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VALIDATOR_PATH = ROOT / "tools" / "validate_m0a.py"
SPEC = importlib.util.spec_from_file_location("validate_m0a", VALIDATOR_PATH)
VALIDATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VALIDATOR)


def digest(payload):
    return hashlib.sha256(VALIDATOR.canonical_bytes(payload)).hexdigest()


class TemporaryBundle:
    """Copy the package so mutation tests never touch authoring files."""

    def __enter__(self):
        self.temp = tempfile.TemporaryDirectory(prefix="tc-bld-m0a-test-")
        self.root = Path(self.temp.name) / "m0a"
        shutil.copytree(ROOT, self.root)
        return self.root

    def __exit__(self, exc_type, exc_value, traceback):
        self.temp.cleanup()


def load_bundle(root=ROOT):
    errors = []
    bundle = VALIDATOR.load_bundle(root, errors)
    if errors:
        raise AssertionError(errors)
    return bundle


class ValidateM0aTests(unittest.TestCase):
    def test_authoring_gate_passes(self):
        errors, _ = VALIDATOR.validate(ROOT, "authoring")
        self.assertEqual([], errors)

    def test_freeze_gate_requires_distinct_audit(self):
        errors, _ = VALIDATOR.validate(ROOT, "freeze")
        self.assertTrue(any("fresh-session audit" in error for error in errors))

    def test_r13_mutation_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "service-components.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            next(item for item in document["components"] if item["ref"] == "R13")["value"] = {
                "decimal": "3.3",
                "unit": "MOhm",
            }
            path.write_text(json.dumps(document), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("R13" in error for error in errors))

    def test_missing_terminal_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "circuit.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            del document["component_terminals"]["D17"]["K"]
            path.write_text(json.dumps(document), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("D17 terminals" in error for error in errors))

    def test_active_device_pin_or_polarity_mutation_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "circuit.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            document["component_terminals"]["D8"] = {
                "A": "N_Q6_COLLECTOR",
                "K": "N_Q5_BASE",
            }
            path.write_text(json.dumps(document), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("active-device/polarity map for D8" in error for error in errors))

    def test_same_node_resistor_short_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "circuit.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            document["component_terminals"]["R48"]["2"] = "N_MODE_R48"
            path.write_text(json.dumps(document), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("R48 is accidentally shorted" in error for error in errors))

    def test_duplicate_json_key_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "service-components.json"
            source = path.read_text(encoding="utf-8")
            source = source.replace('"format":', '"format":"duplicate","format":', 1)
            path.write_text(source, encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("duplicate object key" in error for error in errors))

    def test_observation_overlap_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "documentary-cross-check.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            schematic_group = next(
                item
                for item in document["designator_observation_groups"]
                if item["source"] == "schematic" and item["status"] == "confirmed"
            )
            schematic_group["refs"].append("R1")
            path.write_text(json.dumps(document), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("R1/schematic has duplicate" in error for error in errors))

    def test_variant_classification_gap_is_rejected(self):
        with TemporaryBundle() as root:
            path = root / "assumptions.json"
            document = json.loads(path.read_text(encoding="utf-8"))
            document["variant_classification"]["executable"].remove("P2-REVERSED-SHAFT-SENSE")
            path.write_text(json.dumps(document), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "authoring")
            self.assertTrue(any("classification coverage mismatch" in error for error in errors))

    def test_default_materialization_is_deterministic_and_resolves_boost(self):
        bundle = load_bundle()
        first = VALIDATOR.materialized_payload(bundle)
        second = VALIDATOR.materialized_payload(copy.deepcopy(bundle))
        self.assertEqual(VALIDATOR.canonical_bytes(first), VALIDATOR.canonical_bytes(second))
        self.assertEqual("E", first["component_terminals"]["IC1"]["7"])
        self.assertEqual("E", first["component_terminals"]["C11"]["2"])
        self.assertEqual(["E", "N_MODE_C11", "O1"], first["node_equivalence_classes"]["E"])
        self.assertEqual("channel_off", first["device_states"]["Q1"])
        self.assertEqual("channel_off", first["device_states"]["Q2"])

    def test_executable_alternative_changes_handoff_without_editing_source(self):
        bundle = load_bundle()
        default = VALIDATOR.materialized_payload(bundle)
        alternate = VALIDATOR.materialized_payload(
            bundle,
            {"A-P2-TAPER": "P2-REVERSED-SHAFT-SENSE"},
        )
        self.assertNotEqual(digest(default), digest(alternate))
        self.assertEqual("reverse_normalized_shaft", alternate["orientation_overrides"]["P2"])
        self.assertEqual(
            "P2-REVERSED-SHAFT-SENSE",
            alternate["selected_assumptions"]["A-P2-TAPER"]["variant"],
        )

    def test_evidence_acquisition_variant_refuses_materialization(self):
        bundle = load_bundle()
        with self.assertRaisesRegex(VALIDATOR.MaterializationError, "cannot be materialized"):
            VALIDATOR.materialized_payload(
                bundle,
                {"A-C17-RETURN": "C17-OTHER-CROSSING"},
            )

    def test_power_contact_alternative_changes_resolved_nodes(self):
        bundle = load_bundle()
        selected = {
            "A-SUPPLY-VREF": "SUPPLY-9V-BATTERY-SWITCHED",
        }
        dual_nc = VALIDATOR.materialized_payload(bundle, selected)
        self.assertEqual(
            dual_nc["component_terminals"]["D1"]["A"],
            dual_nc["component_terminals"]["D1"]["K"],
        )
        selected["A-POWER-JACK-CONTACTS"] = "POWER-JACK-BATTERY-THROUGH-D1"
        through_diode = VALIDATOR.materialized_payload(bundle, selected)
        self.assertNotEqual(
            through_diode["component_terminals"]["D1"]["A"],
            through_diode["component_terminals"]["D1"]["K"],
        )

    def test_named_dynamic_configuration_is_explicitly_not_runnable(self):
        bundle = load_bundle()
        payload = VALIDATOR.materialized_payload(
            bundle,
            configuration_id="STATE-ENGAGED-BOOST-DYNAMIC-SUPPRESSOR",
        )
        self.assertEqual("dynamic_suppressor_controlled", payload["device_states"]["Q2"])
        self.assertFalse(payload["oracle_readiness"]["dynamic_latch_or_suppressor_runnable"])

    def test_named_18v_configuration_owns_its_supply_profile(self):
        bundle = load_bundle()
        payload = VALIDATOR.materialized_payload(
            bundle,
            configuration_id="STATE-ENGAGED-BOOST-18V-SENSITIVITY",
        )
        self.assertEqual("SUPPLY-18V-EFFECTIVE-SENSITIVITY-V1", payload["selected_profiles"]["POWER"])
        self.assertFalse(payload["configuration"]["selected_for_first_oracle"])
        self.assertIn(
            "SUPPLY-18V-EFFECTIVE-SENSITIVITY-V1",
            payload["selected_profile_definitions"],
        )

    def test_exact_audit_record_passes_and_stale_file_hash_fails(self):
        with TemporaryBundle() as root:
            errors, bundle = VALIDATOR.validate(root, "authoring")
            self.assertEqual([], errors)
            audit = VALIDATOR.build_audit_template(root, bundle)
            audit.update({
                "id": "AUDIT-TEST-FRESH-SESSION",
                "auditor": "independent-test-auditor",
                "session_id": "fresh-session-test-001",
                "audited_at": "2026-09-03T12:00:00Z",
            })
            cross_path = root / "documentary-cross-check.json"
            cross = json.loads(cross_path.read_text(encoding="utf-8"))
            cross["independent_freeze_audits"].append(audit)
            cross["freeze_gate_status"] = "passed_fresh_session_audit"
            cross_path.write_text(json.dumps(cross), encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "freeze")
            self.assertEqual([], errors)

            readme = root / "README.md"
            readme.write_text(readme.read_text(encoding="utf-8") + "\nchanged after audit\n", encoding="utf-8")
            errors, _ = VALIDATOR.validate(root, "freeze")
            self.assertTrue(any("stale/mismatched audited_file_sha256" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
