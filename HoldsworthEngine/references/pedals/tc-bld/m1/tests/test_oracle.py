#!/usr/bin/env python3
"""Focused checks for the TC BLD M1 offline CLEAN BOOST oracle."""

import hashlib
import importlib.util
import json
import math
import sys
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch


M1_ROOT = Path(__file__).resolve().parents[1]
ORACLE_PATH = M1_ROOT / "tools" / "generate_oracle.py"
GOLDEN_PATH = M1_ROOT / "golden" / "engaged-clean-boost-primary.json"
SPEC = importlib.util.spec_from_file_location("tc_bld_m1_oracle", ORACLE_PATH)
ORACLE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = ORACLE
SPEC.loader.exec_module(ORACLE)


def walk_numbers(value):
    if isinstance(value, dict):
        for nested in value.values():
            yield from walk_numbers(nested)
    elif isinstance(value, list):
        for nested in value:
            yield from walk_numbers(nested)
    elif isinstance(value, float):
        yield value


class MnaPrimitiveTests(unittest.TestCase):
    def test_resistor_divider_matches_closed_form(self):
        system = ORACLE.MnaSystem()
        system.add_constraint("SOURCE", "0V", 1.0, label="source")
        system.add_resistor("SOURCE", "OUT", 1000.0, "upper")
        system.add_resistor("OUT", "0V", 3000.0, "lower")
        voltages, diagnostics = system.solve()
        self.assertAlmostEqual(0.75, voltages["OUT"].real, places=13)
        self.assertLess(diagnostics["maximum_scaled_residual"], 1.0e-13)

    def test_inverting_opamp_fixture_has_expected_sign_and_gain(self):
        system = ORACLE.MnaSystem()
        system.add_constraint("SOURCE", "0V", 1.0, label="source")
        system.add_resistor("SOURCE", "MINUS", 1000.0, "input")
        system.add_resistor("OUT", "MINUS", 10000.0, "feedback")
        gain = 1.0e9
        system.add_constraint(
            "OUT",
            "0V",
            0.0,
            (("PLUS", -gain), ("MINUS", gain)),
            label="opamp",
        )
        system.add_resistor("PLUS", "0V", 1000.0, "plus_reference")
        voltages, diagnostics = system.solve()
        self.assertAlmostEqual(-10.0, voltages["OUT"].real, places=6)
        self.assertLess(diagnostics["maximum_scaled_residual"], 1.0e-12)


class OracleGoldenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        validator, bundle, resolved, digests = ORACLE.load_m0a()
        cls.validator = validator
        cls.bundle = bundle
        cls.resolved = resolved
        cls.digests = digests
        cls.oracle = ORACLE.CleanBoostOracle(bundle, resolved)
        cls.generated = ORACLE.build_payload()
        cls.stored = json.loads(GOLDEN_PATH.read_text(encoding="utf-8"))

    def test_m0a_profile_configuration_and_digests_are_exact(self):
        identity = self.generated["m0a_identity"]
        self.assertEqual(ORACLE.EXPECTED_M0A_PROFILE, identity["profile_id"])
        self.assertEqual(ORACLE.PRIMARY_CONFIGURATION, identity["configuration"])
        self.assertEqual(ORACLE.EXPECTED_M0A_RESOLVED_SHA256, identity["resolved_sha256"])
        self.assertEqual(
            ORACLE.EXPECTED_M0A_NORMATIVE_SHA256,
            identity["normative_bundle_sha256"],
        )
        self.assertEqual({"Q1": "channel_off", "Q2": "channel_off"}, identity["device_states"])
        self.assertEqual("Boost", identity["active_switch_states"]["SW_MODE"])
        self.assertEqual(ORACLE.EXPECTED_SELECTED_PROFILES, identity["selected_profiles"])

    def test_golden_schema_identity_and_internal_digest(self):
        self.assertEqual([], ORACLE.validate_payload(self.generated))
        results = self.generated["results"]
        expected = hashlib.sha256(ORACLE.canonical_bytes(results)).hexdigest()
        self.assertEqual(expected, self.generated["results_sha256"])
        self.assertEqual(list(ORACLE.FREQUENCIES_HZ), results["frequency_grid_hz"])

    def test_reference_node_stamps_match_frozen_m0a_terminals(self):
        # load_m0a() has already verified the independently audited freeze and
        # primary resolved digest. Compare actual stamps with that external
        # representation, not merely another copy of the oracle's node names.
        documentary = self.bundle["circuit.json"]["component_terminals"]
        resolved = self.resolved["component_terminals"]
        for ref, terminal in (
            ("IC1", "10"), ("IC1", "12"), ("R41", "2"),
            ("R44", "2"), ("Q1", "S"), ("Q4", "B"),
        ):
            with self.subTest(ref=ref, terminal=terminal):
                self.assertEqual("NREF_AUDIO", documentary[ref][terminal])
                self.assertEqual(documentary[ref][terminal], resolved[ref][terminal])
        classes = self.resolved["node_equivalence_classes"]
        self.assertEqual(["NREF_AUDIO"], classes["NREF_AUDIO"])
        self.assertEqual(["VREF"], classes["VREF"])
        self.assertNotEqual(resolved["R18"]["1"], resolved["R18"]["2"])

        system = ORACLE.MnaSystem()
        options = self.oracle.default_options
        with patch.object(ORACLE, "MnaSystem", return_value=system), \
                patch.object(system, "add_resistor", wraps=system.add_resistor) as resistors, \
                patch.object(system, "add_capacitor", wraps=system.add_capacitor) as capacitors:
            self.oracle._build(1000.0, options, "transfer", True)

        constraints = {item.label: item for item in system.constraints}
        for label, pin in (("IC1C", "10"), ("IC1D", "12")):
            self.assertEqual(
                resolved["IC1"][pin],
                constraints[f"{label}:open_loop_source"].coefficients[0][0],
            )
        stamps = {call.args[3]: call.args for call in resistors.call_args_list}
        for ref in ("R18", "R19", "R41", "R44"):
            self.assertEqual(
                (resolved[ref]["1"], resolved[ref]["2"]), stamps[ref][:2]
            )
        self.assertEqual(resolved["Q4"]["B"], stamps["Q4_base_reduction"][0])
        self.assertEqual(
            (resolved["Q1"]["D"], resolved["Q1"]["S"]),
            stamps["Q1_off_Rds"][:2],
        )
        capacitors.assert_any_call(
            resolved["Q1"]["D"], resolved["Q1"]["S"],
            options.q1_cds_f, 1.0j * 2.0 * math.pi * 1000.0,
        )

    def test_checked_in_golden_is_exactly_reproducible(self):
        self.assertEqual(ORACLE.pretty_bytes(self.generated), GOLDEN_PATH.read_bytes())
        second = ORACLE.build_payload()
        self.assertEqual(ORACLE.pretty_bytes(self.generated), ORACLE.pretty_bytes(second))

    def test_all_generated_floats_are_finite_and_residuals_are_small(self):
        self.assertTrue(all(math.isfinite(value) for value in walk_numbers(self.generated)))
        numerical = self.generated["results"]["numerical_sanity"]
        self.assertTrue(numerical["all_solutions_finite"])
        self.assertLess(numerical["maximum_scaled_residual_baseline"], 1.0e-10)
        self.assertLess(self.generated["results"]["dc"]["maximum_scaled_residual"], 1.0e-10)

    def test_control_endpoints_and_grid_converge(self):
        for p1 in (0.0, self.oracle.terminal_unity_position, 1.0):
            for p2 in (0.0, 0.5, 1.0):
                for p3 in (0.0, 0.5, 1.0):
                    options = replace(
                        self.oracle.default_options,
                        p1_position=p1,
                        p2_position=p2,
                        p3_position=p3,
                    )
                    for frequency in (5.0, 1000.0, 100000.0):
                        voltages, diagnostics = self.oracle.solve(frequency, options)
                        self.assertTrue(all(math.isfinite(value.real) and math.isfinite(value.imag) for value in voltages.values()))
                        self.assertLess(diagnostics["maximum_scaled_residual"], 1.0e-9)

    def test_dc_vref_and_output_coupling_sanity(self):
        dc = self.generated["results"]["dc"]
        self.assertEqual(9.0, dc["node_voltages_v"]["VPLUS"])
        self.assertGreater(dc["node_voltages_v"]["VREF"], 4.4)
        self.assertLess(dc["node_voltages_v"]["VREF"], 4.6)
        self.assertLess(dc["node_voltages_v"]["NREF_AUDIO"], dc["node_voltages_v"]["VREF"])
        self.assertLessEqual(dc["output_coupled_dc_abs_max_v"], 1.0e-12)

    def test_independent_gain_and_corner_checks(self):
        checks = self.generated["results"]["analytical_checks"]
        expected_gain = 47000.0 / 1500.0
        self.assertAlmostEqual(expected_gain, checks["p1_max_active_gain_v_per_v"], places=10)
        self.assertAlmostEqual(
            20.0 * math.log10(expected_gain),
            checks["p1_max_active_gain_db"],
            places=10,
        )
        self.assertAlmostEqual(
            1.0 / (2.0 * math.pi * 1.0e6 * 47.0e-9),
            checks["c8_r13_simple_corner_hz"],
            places=10,
        )

    def test_terminal_unity_and_controls_have_material_span(self):
        unity = self.oracle.transfer(1000.0, self.oracle.default_options)
        self.assertAlmostEqual(1.0, abs(unity), places=10)
        summary = self.generated["results"]["control_summary"]
        bass = summary["bass"]
        treble = summary["treble"]
        bass_span = abs(bass["p2_0.00"]["100_hz"][0] - bass["p2_1.00"]["100_hz"][0])
        treble_span = abs(
            treble["p3_0.00"]["10000_hz"][0]
            - treble["p3_1.00"]["10000_hz"][0]
        )
        self.assertGreater(bass_span, 10.0)
        self.assertGreater(treble_span, 10.0)

    def test_sensitivity_categories_cover_all_requested_outcomes(self):
        categories = {
            item["classification"]
            for item in self.generated["results"]["source_load_and_assumption_sensitivities"]
        }
        self.assertEqual(
            {
                "negligible_effect",
                "audible_or_potentially_material_effect",
                "major_model_uncertainty",
            },
            categories,
        )


if __name__ == "__main__":
    unittest.main()
