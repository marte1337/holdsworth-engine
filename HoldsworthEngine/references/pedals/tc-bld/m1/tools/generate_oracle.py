#!/usr/bin/env python3
"""Generate the narrow TC BLD M1 engaged-CLEAN-BOOST oracle.

The implementation is deliberately dependency-free.  It solves the selected
M0a small-signal circuit with complex modified nodal analysis (MNA), and emits
deterministic JSON intended to be consumed as M2 golden reference data.

This is a documentary-nominal oracle, not a hardware-calibrated pedal model and
not realtime DSP.  Nonlinear rail/slew/overload behavior belongs to M3.
"""

from __future__ import annotations

import argparse
import cmath
import hashlib
import importlib.util
import json
import math
import sys
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any, Iterable, Optional


M1_ROOT = Path(__file__).resolve().parents[1]
M0A_ROOT = M1_ROOT.parent / "m0a"
M0A_VALIDATOR_PATH = M0A_ROOT / "tools" / "validate_m0a.py"
DEFAULT_GOLDEN_PATH = M1_ROOT / "golden" / "engaged-clean-boost-primary.json"

GOLDEN_FORMAT = "tc-bld-m1-engaged-clean-boost-golden-v1"
ORACLE_ID = "TC-BLD-M1-OFFLINE-MNA-CLEAN-BOOST-V1"
ORACLE_VERSION = 1
PRIMARY_CONFIGURATION = "STATE-ENGAGED-BOOST-HOLDSWORTH-M1-PRIMARY"
EXPECTED_M0A_PROFILE = "TC-BLD-DOC-NOMINAL__S1501-3__B1501-06__L1501-6__AS-M0A-001"
EXPECTED_M0A_RESOLVED_SHA256 = "eba45473391f8f98f855f4516ac610155714e7b101219e16428d2e592085202e"
EXPECTED_M0A_NORMATIVE_SHA256 = "7eac2783d1648242ad1bcf757ace2ea6ddd142df5d483b911dfcac0a3684c1aa"
EXPECTED_SELECTED_PROFILES = {
    "AUDIO_IO": "BOUNDARY-STUDIO-GENERIC-V1",
    "IC1": "OPAMP-4741-FAMILY-GENERIC-V1",
    "INPUT": "INPUT-BOUNDARY-SERVICE-INTERNAL-V1",
    "P1": "TAPER-LOG-10PCT-MID-V1",
    "P2": "TAPER-LIN-IDEAL-V1",
    "P3": "TAPER-LIN-IDEAL-V1",
    "P4": "TAPER-NEGLOG-10PCT-MID-V1",
    "P5": "TAPER-LOG-10PCT-MID-V1",
    "POWER": "SUPPLY-9V-EFFECTIVE-V1",
    "Q1": "JFET-BF245A-DOCUMENTARY-STATIC-V1",
    "Q2": "JFET-BF245A-DOCUMENTARY-STATIC-V1",
    "R35": "R35-PRESET-SERVICE-NOMINAL-MAXIMUM-V1",
}

FREQUENCIES_HZ = tuple(
    float(f"{5.0 * (100000.0 / 5.0) ** (index / 40.0):.12g}")
    for index in range(41)
)
SUMMARY_FREQUENCIES_HZ = (20.0, 100.0, 1000.0, 10000.0, 20000.0)
HEADROOM_FREQUENCIES_HZ = (20.0, 100.0, 1000.0, 10000.0)

OHM_FACTORS = {"Ohm": 1.0, "kOhm": 1.0e3, "MOhm": 1.0e6, "GOhm": 1.0e9}
FARAD_FACTORS = {"pF": 1.0e-12, "nF": 1.0e-9, "uF": 1.0e-6}
VOLT_FACTORS = {"V": 1.0, "mV": 1.0e-3}
FREQUENCY_FACTORS = {"Hz": 1.0, "kHz": 1.0e3, "MHz": 1.0e6}


class OracleError(RuntimeError):
    """Raised when an input identity or numerical solution is invalid."""


def canonical_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        + "\n"
    ).encode("utf-8")


def pretty_bytes(value: Any) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode(
        "utf-8"
    )


def sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def finite_float(value: float, significant_digits: int = 12) -> float:
    if not math.isfinite(value):
        raise OracleError(f"non-finite oracle result {value!r}")
    rounded = float(f"{value:.{significant_digits}g}")
    return 0.0 if rounded == 0.0 else rounded


def quantity(record: dict[str, Any], factors: dict[str, float]) -> float:
    unit = record.get("unit")
    if unit not in factors:
        raise OracleError(f"unsupported quantity unit {unit!r}")
    return float(record["decimal"]) * factors[unit]


def load_m0a() -> tuple[Any, dict[str, dict[str, Any]], dict[str, Any], dict[str, Any]]:
    spec = importlib.util.spec_from_file_location("tc_bld_validate_m0a", M0A_VALIDATOR_PATH)
    if spec is None or spec.loader is None:
        raise OracleError(f"cannot import M0a validator from {M0A_VALIDATOR_PATH}")
    validator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(validator)
    errors, bundle = validator.validate(M0A_ROOT, "freeze")
    if errors:
        raise OracleError("M0a freeze gate failed: " + "; ".join(errors))
    resolved = validator.materialized_payload(
        bundle, configuration_id=PRIMARY_CONFIGURATION
    )
    digests = validator.artifact_digests(M0A_ROOT, bundle, resolved)
    if resolved.get("profile_id") != EXPECTED_M0A_PROFILE:
        raise OracleError("unexpected M0a documentary profile")
    if digests.get("resolved_sha256") != EXPECTED_M0A_RESOLVED_SHA256:
        raise OracleError("M0a resolved handoff digest changed")
    if digests.get("normative_bundle_sha256") != EXPECTED_M0A_NORMATIVE_SHA256:
        raise OracleError("M0a normative bundle digest changed")
    if not resolved.get("oracle_readiness", {}).get("static_engaged_boost_first_oracle"):
        raise OracleError("M0a primary configuration is not ready for the static Boost oracle")
    if resolved.get("selected_profiles") != EXPECTED_SELECTED_PROFILES:
        raise OracleError("M0a selected profile set changed")
    return validator, bundle, resolved, digests


@dataclass(frozen=True)
class ModelOptions:
    p1_position: float
    p2_position: float = 0.5
    p3_position: float = 0.5
    source_resistance_ohm: float = 1.0e3
    load_resistance_ohm: float = 1.0e6
    load_capacitance_f: float = 100.0e-12
    supply_v: float = 9.0
    r35_ohm: float = 2.2e6
    q1_rds_ohm: Optional[float] = 1.0e9
    q1_cds_f: float = 5.0e-12
    q2_rds_ohm: Optional[float] = 1.0e9
    q2_cds_f: float = 5.0e-12
    opamp_model: str = "finite_4741"
    reverse_p1: bool = False
    reverse_p2: bool = False
    reverse_p3: bool = False


@dataclass(frozen=True)
class Constraint:
    positive: str
    negative: str
    value: complex
    coefficients: tuple[tuple[str, complex], ...] = ()
    label: str = ""


class MnaSystem:
    """A compact complex MNA system with deterministic partial pivoting."""

    def __init__(self) -> None:
        self.nodes: set[str] = set()
        self.admittances: list[tuple[str, str, complex]] = []
        self.currents: list[tuple[str, str, complex]] = []
        self.constraints: list[Constraint] = []

    @staticmethod
    def _is_ground(node: str) -> bool:
        return node == "0V"

    def _remember(self, *nodes: str) -> None:
        self.nodes.update(node for node in nodes if not self._is_ground(node))

    def add_admittance(self, first: str, second: str, value: complex) -> None:
        if value == 0.0:
            return
        self._remember(first, second)
        self.admittances.append((first, second, complex(value)))

    def add_resistor(self, first: str, second: str, resistance_ohm: float, label: str) -> None:
        if resistance_ohm < 0.0 or not math.isfinite(resistance_ohm):
            raise OracleError(f"{label}: invalid resistance {resistance_ohm}")
        if resistance_ohm == 0.0:
            self.add_constraint(first, second, 0.0, label=f"{label}:ideal_short")
        else:
            self.add_admittance(first, second, 1.0 / resistance_ohm)

    def add_capacitor(self, first: str, second: str, capacitance_f: float, s: complex) -> None:
        if capacitance_f < 0.0 or not math.isfinite(capacitance_f):
            raise OracleError(f"invalid capacitance {capacitance_f}")
        self.add_admittance(first, second, s * capacitance_f)

    def add_current(self, source: str, destination: str, current_a: complex) -> None:
        self._remember(source, destination)
        self.currents.append((source, destination, complex(current_a)))

    def add_constraint(
        self,
        positive: str,
        negative: str,
        value: complex,
        coefficients: Iterable[tuple[str, complex]] = (),
        label: str = "",
    ) -> None:
        coefficient_tuple = tuple((node, complex(coefficient)) for node, coefficient in coefficients)
        self._remember(positive, negative, *(node for node, _ in coefficient_tuple))
        self.constraints.append(
            Constraint(positive, negative, complex(value), coefficient_tuple, label)
        )

    def solve(self) -> tuple[dict[str, complex], dict[str, float]]:
        node_names = sorted(self.nodes)
        node_index = {node: index for index, node in enumerate(node_names)}
        node_count = len(node_names)
        size = node_count + len(self.constraints)
        matrix = [[0.0j for _ in range(size)] for _ in range(size)]
        rhs = [0.0j for _ in range(size)]

        def stamp(row_node: str, column_node: str, value: complex) -> None:
            if not self._is_ground(row_node) and not self._is_ground(column_node):
                matrix[node_index[row_node]][node_index[column_node]] += value

        for first, second, admittance in self.admittances:
            stamp(first, first, admittance)
            stamp(second, second, admittance)
            stamp(first, second, -admittance)
            stamp(second, first, -admittance)
        for source, destination, current in self.currents:
            if not self._is_ground(source):
                rhs[node_index[source]] -= current
            if not self._is_ground(destination):
                rhs[node_index[destination]] += current
        for offset, constraint in enumerate(self.constraints):
            row = node_count + offset
            if not self._is_ground(constraint.positive):
                index = node_index[constraint.positive]
                matrix[index][row] += 1.0
                matrix[row][index] += 1.0
            if not self._is_ground(constraint.negative):
                index = node_index[constraint.negative]
                matrix[index][row] -= 1.0
                matrix[row][index] -= 1.0
            for node, coefficient in constraint.coefficients:
                if not self._is_ground(node):
                    matrix[row][node_index[node]] += coefficient
            rhs[row] = constraint.value

        solution, diagnostics = solve_dense(matrix, rhs)
        voltages = {node: solution[index] for node, index in node_index.items()}
        voltages["0V"] = 0.0j
        return voltages, diagnostics


def solve_dense(
    matrix: list[list[complex]], rhs: list[complex]
) -> tuple[list[complex], dict[str, float]]:
    size = len(rhs)
    if size == 0 or any(len(row) != size for row in matrix):
        raise OracleError("invalid dense system dimensions")
    original_matrix = [row[:] for row in matrix]
    original_rhs = rhs[:]
    largest_pivot = 0.0
    smallest_pivot = math.inf
    for column in range(size):
        pivot_row = max(range(column, size), key=lambda row: abs(matrix[row][column]))
        pivot = abs(matrix[pivot_row][column])
        if not math.isfinite(pivot) or pivot < 1.0e-18:
            raise OracleError(f"singular/non-finite MNA pivot at column {column}: {pivot}")
        if pivot_row != column:
            matrix[column], matrix[pivot_row] = matrix[pivot_row], matrix[column]
            rhs[column], rhs[pivot_row] = rhs[pivot_row], rhs[column]
        diagonal = matrix[column][column]
        magnitude = abs(diagonal)
        largest_pivot = max(largest_pivot, magnitude)
        smallest_pivot = min(smallest_pivot, magnitude)
        for row in range(column + 1, size):
            if matrix[row][column] == 0.0:
                continue
            factor = matrix[row][column] / diagonal
            matrix[row][column] = 0.0j
            for index in range(column + 1, size):
                matrix[row][index] -= factor * matrix[column][index]
            rhs[row] -= factor * rhs[column]
    result = [0.0j for _ in range(size)]
    for row in range(size - 1, -1, -1):
        remainder = rhs[row]
        for column in range(row + 1, size):
            remainder -= matrix[row][column] * result[column]
        result[row] = remainder / matrix[row][row]
        if not math.isfinite(result[row].real) or not math.isfinite(result[row].imag):
            raise OracleError(f"non-finite dense solution at row {row}")
    maximum_scaled_residual = 0.0
    for row in range(size):
        products = [original_matrix[row][column] * result[column] for column in range(size)]
        residual = abs(sum(products) - original_rhs[row])
        scale = max(abs(original_rhs[row]), sum(abs(product) for product in products), 1.0e-300)
        maximum_scaled_residual = max(maximum_scaled_residual, residual / scale)
    return result, {
        "pivot_ratio_indicator": largest_pivot / smallest_pivot,
        "maximum_scaled_residual": maximum_scaled_residual,
    }


class CleanBoostOracle:
    """Selected M0a engaged-Boost circuit and the M1 linear reductions."""

    def __init__(self, bundle: dict[str, dict[str, Any]], resolved: dict[str, Any]):
        self.bundle = bundle
        self.resolved = resolved
        self.components = {
            item["ref"]: item for item in resolved["service_components"]
        }
        self.profiles = bundle["generic-profiles.json"]
        self.gamma = float(
            self.profiles["tapers"]["TAPER-LOG-10PCT-MID-V1"]["parameters"]["gamma"]
        )
        opamp = self.profiles["active_devices"]["OPAMP-4741-FAMILY-GENERIC-V1"]
        parameters = opamp["parameters"]
        self.opamp_a0 = quantity(parameters["open_loop_gain_typical"], {"V/V": 1.0})
        self.opamp_gbw_hz = quantity(
            parameters["unity_gain_bandwidth_typical"], FREQUENCY_FACTORS
        )
        self.opamp_rout_ohm = quantity(
            parameters["output_resistance_typical"], OHM_FACTORS
        )
        self.r35_primary_ohm = quantity(
            self.profiles["passive_device_profiles"][
                "R35-PRESET-SERVICE-NOMINAL-MAXIMUM-V1"
            ]["selected_effective_resistance"],
            OHM_FACTORS,
        )
        static_states = self.profiles["active_devices"][
            "JFET-BF245A-DOCUMENTARY-STATIC-V1"
        ]["states"]
        self.q_off_rds_ohm = quantity(static_states["channel_off"]["r_ds"], OHM_FACTORS)
        self.q_off_cds_f = quantity(static_states["channel_off"]["c_ds"], FARAD_FACTORS)
        self.primary_source_ohm = quantity(
            self.profiles["boundary_profiles"]["BOUNDARY-STUDIO-GENERIC-V1"][
                "source"
            ]["series_resistance"],
            OHM_FACTORS,
        )
        primary_load = self.profiles["boundary_profiles"]["BOUNDARY-STUDIO-GENERIC-V1"][
            "load"
        ]
        self.primary_load_ohm = quantity(primary_load["resistance"], OHM_FACTORS)
        self.primary_load_f = quantity(primary_load["capacitance"], FARAD_FACTORS)
        self.primary_supply_v = quantity(
            self.profiles["supply_profiles"]["SUPPLY-9V-EFFECTIVE-V1"]["dc"],
            VOLT_FACTORS,
        )
        preliminary_options = ModelOptions(
            p1_position=self.active_gain_unity_shaft_position(),
            source_resistance_ohm=self.primary_source_ohm,
            load_resistance_ohm=self.primary_load_ohm,
            load_capacitance_f=self.primary_load_f,
            supply_v=self.primary_supply_v,
            r35_ohm=self.r35_primary_ohm,
            q1_rds_ohm=self.q_off_rds_ohm,
            q1_cds_f=self.q_off_cds_f,
            q2_rds_ohm=self.q_off_rds_ohm,
            q2_cds_f=self.q_off_cds_f,
        )
        self.default_options = preliminary_options
        self.terminal_unity_position = self._find_terminal_unity_position(
            preliminary_options
        )
        self.default_options = replace(
            preliminary_options, p1_position=self.terminal_unity_position
        )

    def resistance(self, ref: str) -> float:
        return quantity(self.components[ref]["value"], OHM_FACTORS)

    def capacitance(self, ref: str) -> float:
        return quantity(self.components[ref]["value"], FARAD_FACTORS)

    def pot_total(self, ref: str) -> float:
        return self.resistance(ref)

    def active_gain_unity_shaft_position(self) -> float:
        return (1500.0 / 47000.0) ** (1.0 / self.gamma)

    def _find_terminal_unity_position(self, template: ModelOptions) -> float:
        """Find the low-x 1 kHz terminal-unity point for center tone controls."""

        lower = 0.0
        upper = self.active_gain_unity_shaft_position()
        lower_error = abs(self.transfer(1000.0, replace(template, p1_position=lower))) - 1.0
        upper_error = abs(self.transfer(1000.0, replace(template, p1_position=upper))) - 1.0
        if lower_error >= 0.0 or upper_error <= 0.0:
            raise OracleError("cannot bracket the center-tone 1 kHz terminal-unity P1 point")
        for _ in range(80):
            midpoint = 0.5 * (lower + upper)
            error = abs(self.transfer(1000.0, replace(template, p1_position=midpoint))) - 1.0
            if error < 0.0:
                lower = midpoint
            else:
                upper = midpoint
        return 0.5 * (lower + upper)

    def taper_fraction(self, position: float, reverse: bool = False) -> float:
        if not 0.0 <= position <= 1.0:
            raise OracleError(f"pot position outside [0,1]: {position}")
        effective = 1.0 - position if reverse else position
        return effective**self.gamma

    @staticmethod
    def linear_fraction(position: float, reverse: bool = False) -> float:
        if not 0.0 <= position <= 1.0:
            raise OracleError(f"pot position outside [0,1]: {position}")
        return 1.0 - position if reverse else position

    def opamp_parameters(self, frequency_hz: float, model: str) -> tuple[complex, float]:
        if model == "finite_4741":
            pole_hz = self.opamp_gbw_hz / self.opamp_a0
            open_loop = self.opamp_a0 / (1.0 + 1.0j * frequency_hz / pole_hz)
            return open_loop, self.opamp_rout_ohm
        if model == "ideal":
            return complex(1.0e12), 1.0e-6
        raise OracleError(f"unknown op-amp model {model}")

    def _add_opamp(
        self,
        system: MnaSystem,
        output: str,
        noninverting: str,
        inverting: str,
        internal: str,
        frequency_hz: float,
        model: str,
        label: str,
    ) -> None:
        gain, output_resistance = self.opamp_parameters(frequency_hz, model)
        # AC uses the standard differential source Vinternal=A(s)*(V+-V-).
        # At DC only, V+ is the explicitly selected quiescent reference; this
        # supplies a unique midpoint equilibrium for C24's otherwise floating
        # stored voltage without inventing an offset sign.
        noninverting_coefficient = -(gain + 1.0) if frequency_hz == 0.0 else -gain
        system.add_constraint(
            internal,
            "0V",
            0.0,
            ((noninverting, noninverting_coefficient), (inverting, gain)),
            label=f"{label}:open_loop_source",
        )
        system.add_resistor(internal, output, output_resistance, f"{label}:Rout")

    def _build(
        self,
        frequency_hz: float,
        options: ModelOptions,
        excitation: str,
        include_load: bool,
    ) -> MnaSystem:
        if frequency_hz < 0.0:
            raise OracleError("frequency must be non-negative")
        s = 1.0j * 2.0 * math.pi * frequency_hz
        system = MnaSystem()

        # The named effective supply is an ideal post-protection fixture.  Its
        # AC value is zero; DC uses the declared nominal voltage.
        supply_value = options.supply_v if frequency_hz == 0.0 else 0.0
        system.add_constraint("VPLUS", "0V", supply_value, label="VPLUS")
        if excitation == "transfer":
            input_value = 0.0 if frequency_hz == 0.0 else 1.0
            system.add_constraint("SOURCE", "0V", input_value, label="input_source")
            system.add_resistor(
                "SOURCE", "INPUT_TIP", options.source_resistance_ohm, "source_resistance"
            )
        elif excitation == "output_impedance":
            system.add_constraint("SOURCE", "0V", 0.0, label="zeroed_input_source")
            system.add_resistor(
                "SOURCE", "INPUT_TIP", options.source_resistance_ohm, "source_resistance"
            )
            system.add_current("0V", "OUTPUT_HOT", 1.0)
        elif excitation == "input_impedance":
            system.add_current("0V", "INPUT_TIP", 1.0)
        else:
            raise OracleError(f"unknown excitation {excitation}")

        # Finite R2/R3/C1 VREF is retained rather than clamped to half supply.
        system.add_resistor("VPLUS", "VREF", self.resistance("R2"), "R2")
        system.add_resistor("VREF", "0V", self.resistance("R3"), "R3")
        system.add_capacitor("VREF", "0V", self.capacitance("C1"), s)

        # Input coupling/RF network and first 4741 section in selected Boost.
        system.add_capacitor("INPUT_TIP", "N_INPUT_BIAS", self.capacitance("C8"), s)
        system.add_resistor("N_INPUT_BIAS", "VREF", self.resistance("R13"), "R13")
        system.add_resistor("N_INPUT_BIAS", "N_IC1_5", self.resistance("R14"), "R14")
        system.add_capacitor("N_IC1_5", "0V", self.capacitance("C9"), s)
        system.add_resistor("N_IC1_6", "O1", self.resistance("R15"), "R15")
        system.add_capacitor("N_IC1_6", "O1", self.capacitance("C10"), s)
        system.add_capacitor("N_IC1_6", "O1", self.capacitance("C11"), s)
        self._add_opamp(
            system,
            "O1",
            "N_IC1_5",
            "N_IC1_6",
            "N_O1_INTERNAL",
            frequency_hz,
            options.opamp_model,
            "IC1B",
        )

        # E is canonically O1 in the frozen Boost contact state.  The bypassed
        # distortion branch remains connected at both ends and its linear C14
        # loading is retained. D4/DG1 are open at the zero-differential bias.
        system.add_resistor("O1", "N_ASYM_CLAMP", self.resistance("R21"), "R21")
        system.add_resistor(
            "N_ASYM_CLAMP", "N_POST_CLIP", self.resistance("R22"), "R22"
        )
        system.add_resistor("N_POST_CLIP", "O1", self.resistance("R20"), "R20")
        system.add_capacitor("N_POST_CLIP", "VREF", self.capacitance("C14"), s)

        # Active P1 Gain stage. P1 is the terminal-1-to-strapped-wiper rheostat.
        system.add_resistor("O1", "N_R23_C15", self.resistance("R23"), "R23")
        system.add_capacitor("N_R23_C15", "N_IC1_9", self.capacitance("C15"), s)
        p1_fraction = self.taper_fraction(options.p1_position, options.reverse_p1)
        system.add_resistor(
            "N_IC1_9", "OG", self.pot_total("P1") * p1_fraction, "P1"
        )
        self._add_opamp(
            system,
            "OG",
            "NREF_AUDIO",
            "N_IC1_9",
            "N_OG_INTERNAL",
            frequency_hz,
            options.opamp_model,
            "IC1C",
        )

        # Q4 remains physically attached to NREF_AUDIO in Boost.  At DC it is
        # a generic beta/VBE emitter-follower load; AC uses its local linearized
        # base resistance. This is a fixed engineering reduction, not a device fit.
        beta = 200.0
        vbe_v = 0.65
        r17 = self.resistance("R17")
        if frequency_hz == 0.0:
            conductance = 1.0 / ((beta + 1.0) * r17)
            system.add_admittance("NREF_AUDIO", "0V", conductance)
            system.add_current("0V", "NREF_AUDIO", conductance * vbe_v)
        else:
            nominal_nref = self._estimated_nref(options.supply_v)
            emitter_current = max((nominal_nref - vbe_v) / r17, 1.0e-12)
            collector_current = emitter_current * beta / (beta + 1.0)
            r_pi = beta * 0.02585 / collector_current
            system.add_resistor(
                "NREF_AUDIO", "0V", r_pi + (beta + 1.0) * r17, "Q4_base_reduction"
            )
        system.add_resistor("NREF_AUDIO", "VREF", self.resistance("R18"), "R18")

        # Q1 is held in M0a's static channel-off state. The full audio branch is
        # retained; its static control fixture AC-grounds the gate, so C31 also
        # remains as a drain load. No latch dynamics are simulated.
        system.add_capacitor("O1", "N_Q1_DRAIN", self.capacitance("C13"), s)
        system.add_resistor("N_Q1_DRAIN", "NREF_AUDIO", self.resistance("R19"), "R19")
        if options.q1_rds_ohm is not None:
            system.add_resistor(
                "N_Q1_DRAIN", "NREF_AUDIO", options.q1_rds_ohm, "Q1_off_Rds"
            )
        system.add_capacitor("N_Q1_DRAIN", "NREF_AUDIO", options.q1_cds_f, s)
        system.add_capacitor("N_Q1_DRAIN", "0V", self.capacitance("C31"), s)

        # Coupled active Bass/Treble network. P2/P3 zero-resistance endpoints
        # are exact MNA voltage constraints, not epsilon resistors.
        system.add_capacitor("OG", "N_TONE_INPUT", self.capacitance("C16"), s)
        p2_fraction = self.linear_fraction(options.p2_position, options.reverse_p2)
        p2_total = self.pot_total("P2")
        system.add_resistor("N_TONE_INPUT", "N_P2_LEFT", self.resistance("R42"), "R42")
        system.add_resistor("N_P2_LEFT", "N_P2_WIPER", p2_total * p2_fraction, "P2a")
        system.add_resistor(
            "N_P2_WIPER", "N_P2_RIGHT", p2_total * (1.0 - p2_fraction), "P2b"
        )
        system.add_capacitor("N_P2_LEFT", "N_P2_WIPER", self.capacitance("C28"), s)
        system.add_capacitor("N_P2_WIPER", "N_P2_RIGHT", self.capacitance("C27"), s)
        system.add_resistor("N_P2_RIGHT", "NREF_AUDIO", self.resistance("R41"), "R41")
        system.add_resistor(
            "N_P2_WIPER", "N_TONE_SHAPE", self.resistance("R45"), "R45"
        )

        p3_fraction = self.linear_fraction(options.p3_position, options.reverse_p3)
        p3_total = self.pot_total("P3")
        system.add_resistor("N_TONE_INPUT", "N_P3_LEFT", self.resistance("R43"), "R43")
        system.add_resistor("N_P3_LEFT", "N_P3_WIPER", p3_total * p3_fraction, "P3a")
        system.add_resistor(
            "N_P3_WIPER", "N_P3_RIGHT", p3_total * (1.0 - p3_fraction), "P3b"
        )
        system.add_capacitor("N_P3_LEFT", "N_P3_RIGHT", self.capacitance("C29"), s)
        system.add_capacitor("N_P3_WIPER", "N_TONE_SHAPE", self.capacitance("C30"), s)
        system.add_resistor("N_P3_RIGHT", "NREF_AUDIO", self.resistance("R44"), "R44")
        system.add_resistor(
            "N_TONE_SHAPE", "N_IC1_13", self.resistance("R46"), "R46"
        )

        # Final AC feedback, frozen Q2 off-state, and line driver.
        system.add_capacitor("N_IC1_13", "N_Q2_DRAIN", self.capacitance("C24"), s)
        system.add_resistor("N_Q2_DRAIN", "OUT14", self.resistance("R38"), "R38")
        if options.q2_rds_ohm is not None:
            system.add_resistor("N_Q2_DRAIN", "OUT14", options.q2_rds_ohm, "Q2_off_Rds")
        system.add_capacitor("N_Q2_DRAIN", "OUT14", options.q2_cds_f, s)
        self._add_opamp(
            system,
            "OUT14",
            "NREF_AUDIO",
            "N_IC1_13",
            "N_OUT14_INTERNAL",
            frequency_hz,
            options.opamp_model,
            "IC1D",
        )

        # Only the known passive part of the frozen suppressor/control graph is
        # retained: it is the portion that can load OG/OUT14 while Q2 is fixed.
        # D9-D12 and Q5/Q6 are open in this zero-signal linearization.
        system.add_capacitor("OG", "NRETURN", self.capacitance("C17"), s)
        system.add_resistor("NRETURN", "N_R35_R34", self.resistance("R34"), "R34")
        system.add_resistor("N_R35_R34", "OUT14", options.r35_ohm, "R35")
        system.add_resistor("NRETURN", "N_CTL2", self.resistance("R33"), "R33")
        system.add_resistor("N_CTL2", "OUT14", self.resistance("R36"), "R36")
        system.add_capacitor("N_CTL2", "0V", self.capacitance("C22"), s)
        system.add_resistor("N_CTL2", "N_C26_R16", self.resistance("R16"), "R16")
        system.add_capacitor("N_C26_R16", "OUT14", self.capacitance("C26"), s)

        # Shared single-ended output circuit and selected load.
        system.add_capacitor("OUT14", "N_OUTPUT_COUPLED", self.capacitance("C25"), s)
        system.add_resistor("N_OUTPUT_COUPLED", "0V", self.resistance("R39"), "R39")
        system.add_resistor("N_OUTPUT_COUPLED", "OUTPUT_HOT", self.resistance("R40"), "R40")
        if include_load:
            system.add_resistor(
                "OUTPUT_HOT", "0V", options.load_resistance_ohm, "output_load"
            )
            system.add_capacitor("OUTPUT_HOT", "0V", options.load_capacitance_f, s)
        return system

    def _estimated_nref(self, supply_v: float) -> float:
        # Closed-form DC solution of R2/R3, R18, and the generic Q4 base load.
        r2 = self.resistance("R2")
        r3 = self.resistance("R3")
        r18 = self.resistance("R18")
        beta = 200.0
        vbe = 0.65
        q4_resistance = (beta + 1.0) * self.resistance("R17")
        # Solve the two-node linear system explicitly.
        a11 = 1.0 / r2 + 1.0 / r3 + 1.0 / r18
        a12 = -1.0 / r18
        b1 = supply_v / r2
        a21 = -1.0 / r18
        a22 = 1.0 / r18 + 1.0 / q4_resistance
        b2 = vbe / q4_resistance
        determinant = a11 * a22 - a12 * a21
        return (a11 * b2 - a21 * b1) / determinant

    def solve(
        self,
        frequency_hz: float,
        options: Optional[ModelOptions] = None,
        excitation: str = "transfer",
        include_load: bool = True,
    ) -> tuple[dict[str, complex], dict[str, float]]:
        selected = options or self.default_options
        return self._build(frequency_hz, selected, excitation, include_load).solve()

    def transfer(self, frequency_hz: float, options: Optional[ModelOptions] = None) -> complex:
        voltages, _ = self.solve(frequency_hz, options, "transfer", True)
        return voltages["OUTPUT_HOT"]

    def input_impedance(self, frequency_hz: float, options: Optional[ModelOptions] = None) -> complex:
        voltages, _ = self.solve(frequency_hz, options, "input_impedance", True)
        return voltages["INPUT_TIP"]

    def output_impedance(self, frequency_hz: float, options: Optional[ModelOptions] = None) -> complex:
        voltages, _ = self.solve(frequency_hz, options, "output_impedance", False)
        return voltages["OUTPUT_HOT"]


def response_point(value: complex) -> list[float]:
    magnitude_db = 20.0 * math.log10(max(abs(value), 1.0e-300))
    return [finite_float(magnitude_db), finite_float(math.degrees(cmath.phase(value)))]


def impedance_point(value: complex) -> list[float]:
    return [
        finite_float(value.real),
        finite_float(value.imag),
        finite_float(abs(value)),
        finite_float(math.degrees(cmath.phase(value))),
    ]


def response_curve(oracle: CleanBoostOracle, options: ModelOptions) -> list[list[float]]:
    return [response_point(oracle.transfer(frequency, options)) for frequency in FREQUENCIES_HZ]


def named_curve(
    oracle: CleanBoostOracle,
    curve_id: str,
    options: ModelOptions,
    **metadata: Any,
) -> dict[str, Any]:
    return {
        "id": curve_id,
        "controls": {
            "p1_shaft": finite_float(options.p1_position),
            "p2_electrical_x": finite_float(options.p2_position),
            "p3_electrical_x": finite_float(options.p3_position),
        },
        **metadata,
        "gain_phase": response_curve(oracle, options),
    }


def interpolate_log_frequency(curve: list[list[float]], target_hz: float) -> list[float]:
    if target_hz <= FREQUENCIES_HZ[0]:
        return curve[0]
    if target_hz >= FREQUENCIES_HZ[-1]:
        return curve[-1]
    target = math.log(target_hz)
    for index, upper in enumerate(FREQUENCIES_HZ[1:], 1):
        if upper >= target_hz:
            lower = FREQUENCIES_HZ[index - 1]
            fraction = (target - math.log(lower)) / (math.log(upper) - math.log(lower))
            return [
                finite_float(curve[index - 1][column] * (1.0 - fraction) + curve[index][column] * fraction)
                for column in range(2)
            ]
    raise AssertionError("unreachable frequency interpolation")


def curve_delta(
    reference: list[list[float]], alternate: list[list[float]], maximum_hz: float = 20000.0
) -> dict[str, float]:
    indices = [index for index, frequency in enumerate(FREQUENCIES_HZ) if 20.0 <= frequency <= maximum_hz]
    magnitude = max(abs(alternate[index][0] - reference[index][0]) for index in indices)
    phase = max(
        abs(((alternate[index][1] - reference[index][1] + 180.0) % 360.0) - 180.0)
        for index in indices
    )
    return {
        "max_abs_magnitude_db_20hz_to_20khz": finite_float(magnitude),
        "max_abs_phase_deg_20hz_to_20khz": finite_float(phase),
    }


def classify_delta(delta: dict[str, float]) -> str:
    if (
        delta["max_abs_magnitude_db_20hz_to_20khz"] < 0.1
        and delta["max_abs_phase_deg_20hz_to_20khz"] < 1.0
    ):
        return "negligible_effect"
    return "audible_or_potentially_material_effect"


def control_sweeps(oracle: CleanBoostOracle) -> dict[str, Any]:
    base = oracle.default_options
    active_unity = oracle.active_gain_unity_shaft_position()
    terminal_unity = oracle.terminal_unity_position
    gain_positions = (0.1, terminal_unity, 0.25, active_unity, 0.5, 0.75, 1.0)
    linear_positions = (0.0, 0.25, 0.5, 0.75, 1.0)
    gain = [
        named_curve(
            oracle,
            f"p1_{position:.9g}",
            replace(base, p1_position=position),
            analytical_feedback_resistance_ohm=finite_float(
                oracle.pot_total("P1") * oracle.taper_fraction(position)
            ),
            analytical_active_gain_db=finite_float(
                20.0
                * math.log10(
                    oracle.pot_total("P1")
                    * oracle.taper_fraction(position)
                    / oracle.resistance("R23")
                )
            ),
            is_active_gain_analytical_unity=math.isclose(
                position, active_unity, rel_tol=1.0e-12
            ),
            is_terminal_1khz_unity=math.isclose(
                position, terminal_unity, rel_tol=1.0e-12
            ),
            is_high_boost=position >= 0.75,
        )
        for position in gain_positions
    ]
    bass = [
        named_curve(
            oracle,
            f"p2_{position:.2f}",
            replace(base, p2_position=position),
            held={"p1": "terminal_1khz_unity_at_center_tone", "p3": 0.5},
        )
        for position in linear_positions
    ]
    treble = [
        named_curve(
            oracle,
            f"p3_{position:.2f}",
            replace(base, p3_position=position),
            held={"p1": "terminal_1khz_unity_at_center_tone", "p2": 0.5},
        )
        for position in linear_positions
    ]
    combined = []
    for p2 in (0.0, 0.5, 1.0):
        for p3 in (0.0, 0.5, 1.0):
            combined.append(
                named_curve(
                    oracle,
                    f"p2_{p2:.1f}_p3_{p3:.1f}",
                    replace(base, p2_position=p2, p3_position=p3),
                    held={"p1": "terminal_1khz_unity_at_center_tone"},
                )
            )
    return {"gain": gain, "bass": bass, "treble": treble, "combined_tone": combined}


def impedance_curves(oracle: CleanBoostOracle) -> dict[str, Any]:
    options = oracle.default_options
    return {
        "definition": {
            "input": "intrinsic INPUT_TIP impedance with external source fixture removed",
            "output": "intrinsic OUTPUT_HOT impedance with input source zeroed and declared load removed",
            "columns": ["real_ohm", "imag_ohm", "magnitude_ohm", "phase_deg"],
        },
        "input": [impedance_point(oracle.input_impedance(frequency, options)) for frequency in FREQUENCIES_HZ],
        "output": [impedance_point(oracle.output_impedance(frequency, options)) for frequency in FREQUENCIES_HZ],
    }


def sensitivity_cases(oracle: CleanBoostOracle) -> list[dict[str, Any]]:
    base = oracle.default_options
    primary = response_curve(oracle, base)
    cases: list[dict[str, Any]] = []

    def append_case(
        case_id: str,
        assumption_id: str,
        variant_or_case: str,
        alternate_options: ModelOptions,
        note: str,
    ) -> None:
        alternate = response_curve(oracle, alternate_options)
        delta = curve_delta(primary, alternate)
        cases.append(
            {
                "id": case_id,
                "assumption_id": assumption_id,
                "variant_or_case": variant_or_case,
                "classification": classify_delta(delta),
                "delta": delta,
                "note": note,
                "gain_phase": alternate,
            }
        )

    append_case(
        "q1_ideal_open",
        "A-Q1-STATE",
        "Q1-IDEAL-OPEN-SENSITIVITY",
        replace(base, q1_rds_ohm=None, q1_cds_f=0.0),
        "Only the generic Q1 channel Rds/Cds is removed; C13/R19/C31 remain.",
    )
    append_case(
        "q2_ideal_open_engineering_bound",
        "A-Q2-STATE",
        "M1-ONLY-Q2-IDEAL-OPEN-BOUND",
        replace(base, q2_rds_ohm=None, q2_cds_f=0.0),
        "Engineering bound for the numerical off-state envelope; not a new M0a variant.",
    )
    append_case(
        "r35_midpoint",
        "A-R35-SYMBOL",
        "R35-PRESET-MIDPOINT-SENSITIVITY-V1",
        replace(base, r35_ohm=1.1e6),
        "Uses the frozen M0a generic midpoint profile without selecting a fitted value.",
    )
    append_case(
        "ideal_opamp",
        "A-4741-GENERIC",
        "IDEAL-OPAMP-SENSITIVITY",
        replace(base, opamp_model="ideal"),
        "Topology-only comparison against the selected finite A0/GBW/Rout mapping.",
    )
    append_case(
        "effective_18v_supply",
        "A-SUPPLY-VREF",
        "SUPPLY-18V-SENSITIVITY",
        replace(base, supply_v=18.0),
        "The incremental response barely moves; the mapped stage-headroom bound changes materially. This is a documentary sensitivity, not a hardware recommendation.",
    )
    supply_case = cases[-1]
    supply_case["small_signal_classification"] = supply_case["classification"]
    dc_9v, _ = oracle.solve(0.0, base, "transfer", True)
    dc_18v, _ = oracle.solve(0.0, replace(base, supply_v=18.0), "transfer", True)
    margin_9v = min(dc_9v["OUT14"].real - 1.3, 9.0 - 1.3 - dc_9v["OUT14"].real)
    margin_18v = min(dc_18v["OUT14"].real - 1.3, 18.0 - 1.3 - dc_18v["OUT14"].real)
    supply_case["mapped_headroom_margin_change_db"] = finite_float(
        20.0 * math.log10(margin_18v / margin_9v)
    )
    supply_case["classification"] = "audible_or_potentially_material_effect"
    append_case(
        "p1_reversed_shaft",
        "A-P1-TAPER",
        "P1-REVERSED-SHAFT-SENSE",
        replace(base, p1_position=0.25, reverse_p1=True),
        "Compared at the same mechanical x=0.25; the electrical response exists in the primary sweep at x=0.75.",
    )
    p1_forward_quarter = response_curve(oracle, replace(base, p1_position=0.25))
    cases[-1]["delta"] = curve_delta(p1_forward_quarter, cases[-1]["gain_phase"])
    cases[-1]["classification"] = classify_delta(cases[-1]["delta"])
    append_case(
        "p2_reversed_shaft",
        "A-P2-TAPER",
        "P2-REVERSED-SHAFT-SENSE",
        replace(base, p2_position=0.25, reverse_p2=True),
        "Compared at the same mechanical x=0.25; control direction is documentary-uncertain.",
    )
    p2_forward_quarter = response_curve(oracle, replace(base, p2_position=0.25))
    cases[-1]["delta"] = curve_delta(p2_forward_quarter, cases[-1]["gain_phase"])
    cases[-1]["classification"] = classify_delta(cases[-1]["delta"])
    append_case(
        "p3_reversed_shaft",
        "A-P3-TAPER",
        "P3-REVERSED-SHAFT-SENSE",
        replace(base, p3_position=0.25, reverse_p3=True),
        "Compared at the same mechanical x=0.25; control direction is documentary-uncertain.",
    )
    p3_forward_quarter = response_curve(oracle, replace(base, p3_position=0.25))
    cases[-1]["delta"] = curve_delta(p3_forward_quarter, cases[-1]["gain_phase"])
    cases[-1]["classification"] = classify_delta(cases[-1]["delta"])
    append_case(
        "source_10k",
        "A-SOURCE-LOAD",
        "M1-REPRESENTATIVE-10K-SOURCE",
        replace(base, source_resistance_ohm=10.0e3),
        "Declared M1 source sensitivity; primary remains the M0a 1 kOhm source.",
    )
    append_case(
        "source_100k",
        "A-SOURCE-LOAD",
        "M1-REPRESENTATIVE-100K-SOURCE",
        replace(base, source_resistance_ohm=100.0e3),
        "Declared M1 high-source-resistance sensitivity; not a pickup/cable calibration.",
    )
    append_case(
        "load_10k",
        "A-SOURCE-LOAD",
        "BOUNDARY-10K-LOAD-SENSITIVITY-V1",
        replace(base, load_resistance_ohm=10.0e3),
        "Uses the named M0a 10 kOhm load sensitivity profile.",
    )

    cases.extend(
        [
            {
                "id": "mode_crossbar_alternate",
                "assumption_id": "A-MODE-CONTACTS",
                "variant_or_case": "MODE-CROSSBAR-ALTERNATE",
                "classification": "major_model_uncertainty",
                "note": "M0a intentionally refuses speculative rewiring; no electrically defined alternate can be run.",
            },
            {
                "id": "c17_other_crossing",
                "assumption_id": "A-C17-RETURN",
                "variant_or_case": "C17-OTHER-CROSSING",
                "classification": "major_model_uncertainty",
                "note": "M0a provides no alternate destination node, so the effect cannot be bounded without inventing topology.",
            },
            {
                "id": "dynamic_q2_suppressor",
                "assumption_id": "A-Q2-STATE",
                "variant_or_case": "Q2-DYNAMIC-SUPPRESSOR",
                "classification": "major_model_uncertainty",
                "note": "Outside narrow M1 and explicitly not runnable in M0a; M2 uses the frozen full-level state.",
            },
            {
                "id": "power_contact_variants_under_effective_vplus",
                "assumption_id": "A-POWER-JACK-CONTACTS",
                "variant_or_case": "POWER-JACK-DUAL-NC-V1_vs_POWER-JACK-BATTERY-THROUGH-D1",
                "classification": "negligible_effect",
                "delta": {
                    "max_abs_magnitude_db_20hz_to_20khz": 0.0,
                    "max_abs_phase_deg_20hz_to_20khz": 0.0,
                },
                "note": "Exact no-effect in the selected post-protection SUPPLY-9V-EFFECTIVE-V1 fixture; not a claim about battery operation.",
            },
            {
                "id": "p4_orientation_small_signal",
                "assumption_id": "A-P4-TAPER",
                "variant_or_case": "P4-ENDS-REVERSED",
                "classification": "negligible_effect",
                "delta": {
                    "max_abs_magnitude_db_20hz_to_20khz": 0.0,
                    "max_abs_phase_deg_20hz_to_20khz": 0.0,
                },
                "note": "P4 is isolated by the selected Boost contacts; the fixed R21/R22/R20/C14 branch is retained separately. Large-signal diode loading remains M3.",
            },
            {
                "id": "jfet_drain_source_swap_linear",
                "assumption_id": "A-JFET-DS-IDENTITY",
                "variant_or_case": "JFET-DS-SWAPPED",
                "classification": "negligible_effect",
                "delta": {
                    "max_abs_magnitude_db_20hz_to_20khz": 0.0,
                    "max_abs_phase_deg_20hz_to_20khz": 0.0,
                },
                "note": "Exact no-effect for the bilateral Rds/Cds static reduction; nonlinear/parasitic asymmetry is deferred.",
            },
        ]
    )
    return cases


def manual_input_boundary_sanity() -> dict[str, Any]:
    service_ohm = 1.0e6
    manual_ohm = 3.15e6
    rows = []
    for source_ohm in (1.0e3, 10.0e3, 100.0e3):
        service_ratio = service_ohm / (source_ohm + service_ohm)
        manual_ratio = manual_ohm / (source_ohm + manual_ohm)
        rows.append(
            {
                "source_resistance_ohm": source_ohm,
                "service_1m_voltage_ratio": finite_float(service_ratio),
                "manual_3m15_voltage_ratio": finite_float(manual_ratio),
                "manual_minus_service_db": finite_float(
                    20.0 * math.log10(manual_ratio / service_ratio)
                ),
            }
        )
    return {
        "assumption_id": "A-R13-BOUNDARY",
        "classification": "audible_or_potentially_material_effect",
        "method": "independent real-resistance Thevenin divider only",
        "rows": rows,
        "note": "The manual target is kept upstream and never mutates/parallels service R13. A real passive pickup/cable requires a complex source model and is not reconstructed here.",
    }


def dc_results(oracle: CleanBoostOracle) -> dict[str, Any]:
    voltages, diagnostics = oracle.solve(0.0, oracle.default_options, "transfer", True)
    selected_nodes = (
        "VPLUS",
        "VREF",
        "N_INPUT_BIAS",
        "N_IC1_5",
        "N_IC1_6",
        "O1",
        "NREF_AUDIO",
        "N_IC1_9",
        "OG",
        "N_IC1_13",
        "OUT14",
        "N_OUTPUT_COUPLED",
        "OUTPUT_HOT",
    )
    node_voltages = {node: finite_float(voltages[node].real) for node in selected_nodes}
    return {
        "node_voltages_v": node_voltages,
        "ideal_unloaded_half_supply_v": 4.5,
        "vref_error_from_half_supply_mv": finite_float((voltages["VREF"].real - 4.5) * 1000.0),
        "nref_minus_vref_mv": finite_float(
            (voltages["NREF_AUDIO"].real - voltages["VREF"].real) * 1000.0
        ),
        "output_coupled_dc_abs_max_v": finite_float(
            max(abs(voltages["N_OUTPUT_COUPLED"].real), abs(voltages["OUTPUT_HOT"].real))
        ),
        "pivot_ratio_indicator": finite_float(diagnostics["pivot_ratio_indicator"]),
        "maximum_scaled_residual": finite_float(diagnostics["maximum_scaled_residual"]),
        "equilibrium_note": "The final C24 AC-feedback capacitor has an initial charge selecting the zero-signal midpoint equilibrium; generic 4741 offset/bias signs are not asserted at 9 V.",
    }


def headroom_results(oracle: CleanBoostOracle) -> dict[str, Any]:
    cases = []
    for supply_v in (9.0, 18.0):
        rail_headroom_v = 1.3
        for gain_id, p1 in (("terminal_1khz_unity", oracle.terminal_unity_position), ("maximum", 1.0)):
            options = replace(oracle.default_options, supply_v=supply_v, p1_position=p1)
            dc, _ = oracle.solve(0.0, options, "transfer", True)
            rows = []
            for frequency in HEADROOM_FREQUENCIES_HZ:
                ac, _ = oracle.solve(frequency, options, "transfer", True)
                candidates = []
                for node in ("O1", "OG", "OUT14"):
                    margin = min(
                        dc[node].real - rail_headroom_v,
                        supply_v - rail_headroom_v - dc[node].real,
                    )
                    stage_gain = abs(ac[node])
                    source_peak = margin / stage_gain if stage_gain > 0.0 else math.inf
                    candidates.append((source_peak, node, margin, stage_gain))
                source_peak, limiting_node, margin, stage_gain = min(candidates)
                rows.append(
                    {
                        "frequency_hz": frequency,
                        "estimated_max_source_peak_v": finite_float(source_peak),
                        "estimated_max_source_rms_v": finite_float(source_peak / math.sqrt(2.0)),
                        "limiting_stage_node": limiting_node,
                        "limiting_stage_dc_margin_v": finite_float(margin),
                        "limiting_stage_small_signal_gain_v_per_v": finite_float(stage_gain),
                    }
                )
            cases.append(
                {
                    "supply_v": supply_v,
                    "gain_setting": gain_id,
                    "p1_shaft": finite_float(p1),
                    "rows": rows,
                }
            )
    return {
        "method": "small-signal stage-gain bound; no clipping, slew, overload, or recovery simulation",
        "single_supply_mapping": "Preserve the generic 4741 10 kOhm family-typical 1.3 V rail distance (13.7 V swing on +/-15 V) at each rail; this is an explicit sensitivity mapping, not measured 9 V behavior.",
        "nominal_9v_is_primary": True,
        "18v_is_documented_sensitivity_not_hardware_recommendation": True,
        "cases": cases,
    }


def analytical_checks(oracle: CleanBoostOracle) -> dict[str, Any]:
    active_gain = oracle.pot_total("P1") / oracle.resistance("R23")
    input_corner = 1.0 / (
        2.0 * math.pi * oracle.resistance("R13") * oracle.capacitance("C8")
    )
    gain_coupling_corner = 1.0 / (
        2.0 * math.pi * oracle.resistance("R23") * oracle.capacitance("C15")
    )
    output_return = 1.0 / (
        1.0 / oracle.resistance("R39")
        + 1.0 / (oracle.resistance("R40") + oracle.primary_load_ohm)
    )
    output_corner = 1.0 / (2.0 * math.pi * output_return * oracle.capacitance("C25"))
    return {
        "p1_max_active_gain_v_per_v": finite_float(active_gain),
        "p1_max_active_gain_db": finite_float(20.0 * math.log10(active_gain)),
        "p1_unity_feedback_resistance_ohm": oracle.resistance("R23"),
        "p1_active_gain_unity_shaft_position_generic_log": finite_float(
            oracle.active_gain_unity_shaft_position()
        ),
        "p1_full_oracle_terminal_unity_shaft_position_at_1khz_center_tone": finite_float(
            oracle.terminal_unity_position
        ),
        "unloaded_r2_r3_vref_v": finite_float(
            oracle.primary_supply_v
            * oracle.resistance("R3")
            / (oracle.resistance("R2") + oracle.resistance("R3"))
        ),
        "c8_r13_simple_corner_hz": finite_float(input_corner),
        "c15_r23_simple_corner_hz": finite_float(gain_coupling_corner),
        "c25_r39_parallel_load_simple_corner_hz": finite_float(output_corner),
        "formulae": {
            "maximum_active_gain": "P1_total/R23 = 47k/1.5k",
            "generic_log_unity_shaft": "x=(R23/P1_total)^(1/gamma)",
            "unloaded_vref": "VPLUS*R3/(R2+R3)",
        },
    }


def summarize_controls(sweeps: dict[str, Any]) -> dict[str, Any]:
    def points(curve: dict[str, Any]) -> dict[str, list[float]]:
        return {
            f"{frequency:g}_hz": interpolate_log_frequency(curve["gain_phase"], frequency)
            for frequency in SUMMARY_FREQUENCIES_HZ
        }

    return {
        "columns": ["gain_db", "phase_deg"],
        "gain": {curve["id"]: points(curve) for curve in sweeps["gain"]},
        "bass": {curve["id"]: points(curve) for curve in sweeps["bass"]},
        "treble": {curve["id"]: points(curve) for curve in sweeps["treble"]},
        "combined_tone": {
            curve["id"]: points(curve) for curve in sweeps["combined_tone"]
        },
    }


def build_payload() -> dict[str, Any]:
    validator, bundle, resolved, digests = load_m0a()
    del validator
    oracle = CleanBoostOracle(bundle, resolved)
    dc = dc_results(oracle)
    sweeps = control_sweeps(oracle)
    impedance = impedance_curves(oracle)
    sensitivities = sensitivity_cases(oracle)
    headroom = headroom_results(oracle)
    checks = analytical_checks(oracle)
    baseline = response_curve(oracle, oracle.default_options)
    maximum_pivot_ratio = 0.0
    maximum_scaled_residual = 0.0
    for frequency in FREQUENCIES_HZ:
        _, diagnostics = oracle.solve(frequency, oracle.default_options, "transfer", True)
        maximum_pivot_ratio = max(
            maximum_pivot_ratio, diagnostics["pivot_ratio_indicator"]
        )
        maximum_scaled_residual = max(
            maximum_scaled_residual, diagnostics["maximum_scaled_residual"]
        )

    results = {
        "frequency_grid_hz": list(FREQUENCIES_HZ),
        "gain_phase_columns": ["gain_db", "phase_deg"],
        "dc": dc,
        "baseline_unity_center": {
            "controls": {
                "p1_shaft": finite_float(oracle.terminal_unity_position),
                "p2_electrical_x": 0.5,
                "p3_electrical_x": 0.5,
            },
            "gain_phase": baseline,
        },
        "control_sweeps": sweeps,
        "control_summary": summarize_controls(sweeps),
        "impedance": impedance,
        "source_load_and_assumption_sensitivities": sensitivities,
        "manual_input_boundary_sanity": manual_input_boundary_sanity(),
        "headroom_bounds": headroom,
        "analytical_checks": checks,
        "numerical_sanity": {
            "all_solutions_finite": True,
            "maximum_pivot_ratio_indicator_baseline": finite_float(maximum_pivot_ratio),
            "maximum_scaled_residual_baseline": finite_float(maximum_scaled_residual),
            "pivot_rule": "deterministic maximum-magnitude partial pivoting; reject pivot < 1e-18",
        },
    }
    payload = {
        "format": GOLDEN_FORMAT,
        "oracle_id": ORACLE_ID,
        "oracle_version": ORACLE_VERSION,
        "claim_level": "documentary_nominal_schematic_derived_not_hardware_calibrated",
        "purpose": "M2 engaged CLEAN BOOST small-signal development golden reference",
        "m0a_identity": {
            "profile_id": resolved["profile_id"],
            "source_sha256": resolved["source_sha256"],
            "configuration": resolved["configuration"]["id"],
            "resolved_sha256": digests["resolved_sha256"],
            "normative_bundle_sha256": digests["normative_bundle_sha256"],
            "validator_sha256": digests["validator_sha256"],
            "selected_profiles": resolved["selected_profiles"],
            "device_states": resolved["device_states"],
            "active_switch_states": resolved["active_switch_states"],
        },
        "solver": {
            "architecture": "dependency-free complex dense modified nodal analysis",
            "implementation": "m1/tools/generate_oracle.py",
            "opamp_mapping": {
                "selected_profile": "OPAMP-4741-FAMILY-GENERIC-V1",
                "used": [
                    "open_loop_gain_typical=50000 V/V",
                    "unity_gain_bandwidth_typical=3.5 MHz",
                    "output_resistance_typical=300 Ohm",
                ],
                "equation": "AC: Vinternal=A(s)*(Vnoninv-Vinv), A(s)=A0/(1+s/(2*pi*GBW/A0)); DC adds Vnoninv as the selected quiescent reference",
                "dc_mapping": "zero-signal midpoint equilibrium; input offset and bias-current signs are not asserted at the undocumented single-supply operating point",
            },
            "q4_loading_reduction": {
                "beta": 200.0,
                "vbe_v": 0.65,
                "thermal_voltage_v": 0.02585,
                "claim": "generic fixed emitter-follower load required by the NREF_AUDIO topology; not fitted or hardware-calibrated",
            },
        },
        "scope": {
            "included": [
                "finite R2/R3/C1 VREF",
                "input coupling/RF and first 4741 Boost follower",
                "retained R21/R22/R20/C14 Boost-mode distortion-branch loading",
                "P1 active Gain with frozen generic LOG law",
                "NREF_AUDIO and generic Q4 base/emitter loading",
                "Q1 complete audio branch with frozen off-state Rds/Cds and gate-fixed C31 load",
                "coupled P2/P3 active tone network",
                "C24/R38/Q2-off final feedback and final 4741 section",
                "C17/R33-R36/R16/C22/C26 known passive control-side loading",
                "C25/R39/R40 output and declared load",
            ],
            "excluded_or_reduced": [
                "electronic latch, footswitch, remote, and LED dynamics",
                "dynamic Noise Suppressor detector/envelope and Q2 transfer",
                "Distortion-mode nonlinear conduction; zero-bias D4/DG1 and D14/D15 are open in small signal",
                "4741 slew, rail overload, recovery, output-current limiting, noise, and transient nonlinearities",
                "hardware calibration, aging, tolerance fitting, pickup/cable de-embedding, realtime DSP, UI, and production controls",
            ],
        },
        "classification_thresholds": {
            "negligible_effect": "<0.1 dB and <1 degree maximum delta from 20 Hz to 20 kHz",
            "audible_or_potentially_material_effect": "at or above either numerical threshold",
            "major_model_uncertainty": "M0a intentionally supplies no electrically defined/runnable alternate or the dynamic behavior is outside M1",
        },
        "results": results,
    }
    payload["results_sha256"] = sha256_bytes(canonical_bytes(results))
    return payload


def validate_payload(payload: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    if payload.get("format") != GOLDEN_FORMAT:
        errors.append("golden format mismatch")
    if payload.get("oracle_id") != ORACLE_ID or payload.get("oracle_version") != ORACLE_VERSION:
        errors.append("oracle identity/version mismatch")
    identity = payload.get("m0a_identity", {})
    if identity.get("profile_id") != EXPECTED_M0A_PROFILE:
        errors.append("M0a profile association mismatch")
    if identity.get("configuration") != PRIMARY_CONFIGURATION:
        errors.append("M0a configuration association mismatch")
    if identity.get("resolved_sha256") != EXPECTED_M0A_RESOLVED_SHA256:
        errors.append("M0a resolved digest association mismatch")
    if identity.get("normative_bundle_sha256") != EXPECTED_M0A_NORMATIVE_SHA256:
        errors.append("M0a normative digest association mismatch")
    if identity.get("device_states") != {"Q1": "channel_off", "Q2": "channel_off"}:
        errors.append("M0a static Q1/Q2 state association mismatch")
    if identity.get("selected_profiles") != EXPECTED_SELECTED_PROFILES:
        errors.append("M0a selected profile association mismatch")
    results = payload.get("results")
    if not isinstance(results, dict):
        errors.append("results object missing")
    elif payload.get("results_sha256") != sha256_bytes(canonical_bytes(results)):
        errors.append("results digest mismatch")
    frequencies = results.get("frequency_grid_hz", []) if isinstance(results, dict) else []
    if frequencies != list(FREQUENCIES_HZ):
        errors.append("frequency grid mismatch")
    if isinstance(results, dict) and not results.get("numerical_sanity", {}).get(
        "all_solutions_finite"
    ):
        errors.append("finite-solution assertion missing")
    try:
        maximum_db = results["analytical_checks"]["p1_max_active_gain_db"]
        if not math.isclose(maximum_db, 29.9201319776, rel_tol=0.0, abs_tol=1.0e-9):
            errors.append("maximum active Gain analytical check mismatch")
        vref = results["dc"]["node_voltages_v"]["VREF"]
        if not 4.4 < vref < 4.6:
            errors.append("VREF sanity range failed")
        if results["dc"]["output_coupled_dc_abs_max_v"] > 1.0e-9:
            errors.append("output coupling DC sanity failed")
        if results["numerical_sanity"]["maximum_scaled_residual_baseline"] > 1.0e-10:
            errors.append("baseline MNA residual sanity failed")
    except (KeyError, TypeError):
        errors.append("required analytical/DC fields missing")
    return errors


def write_golden(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(pretty_bytes(payload))


def main(argv: Optional[list[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=DEFAULT_GOLDEN_PATH)
    parser.add_argument("--check", action="store_true", help="compare generated bytes with --output")
    parser.add_argument("--stdout", action="store_true", help="print generated JSON instead of writing")
    args = parser.parse_args(argv)
    try:
        payload = build_payload()
        errors = validate_payload(payload)
        if errors:
            raise OracleError("generated payload failed validation: " + "; ".join(errors))
        generated = pretty_bytes(payload)
        if args.check:
            if not args.output.is_file():
                raise OracleError(f"golden file does not exist: {args.output}")
            stored = args.output.read_bytes()
            if stored != generated:
                raise OracleError(
                    f"golden output is stale: expected sha256 {sha256_bytes(generated)}, "
                    f"stored sha256 {sha256_bytes(stored)}"
                )
            print(f"PASS reproducible golden: {args.output}")
            print(f"sha256={sha256_bytes(stored)}")
        elif args.stdout:
            sys.stdout.buffer.write(generated)
        else:
            write_golden(args.output, payload)
            print(f"wrote {args.output}")
            print(f"sha256={sha256_bytes(generated)}")
        return 0
    except (OSError, OracleError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
