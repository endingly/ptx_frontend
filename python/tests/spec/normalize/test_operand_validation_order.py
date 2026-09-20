"""Lock observable first-error precedence at normalizer extraction boundaries.

Every case contains two independent failures. After checking the original first
error, repair only that error and require the second diagnostic. This avoids a
false ordering test in which the supposed second failure was never reachable.
"""

from copy import deepcopy
from dataclasses import dataclass
from typing import Any
import unittest

from ptx_frontend.spec.normalize.operands import normalize_operand

_REMOVE = object()


@dataclass(frozen=True)
class _OrderCase:
    name: str
    raw: dict[str, Any]
    first_type: type[Exception]
    first_message: str
    repair: dict[str, Any]
    second_type: type[Exception]
    second_message: str


def _raw(kind: str = "reg", **fields: Any) -> dict[str, Any]:
    return {"name": "x", "kind": kind, **fields}


_CASES = (
    _OrderCase(
        "destination flag before predicate flag",
        _raw("shfl_dest", allow_destination_sink=1, allow_predicate_sink=1),
        TypeError,
        "allow_destination_sink must be a boolean when supplied.",
        {"allow_destination_sink": False},
        TypeError,
        "allow_predicate_sink must be a boolean when supplied.",
    ),
    _OrderCase(
        "shfl flags before mbarrier form",
        _raw("shfl_dest", allow_predicate_sink=1, mbarrier_state_token_form="invalid"),
        TypeError,
        "allow_predicate_sink must be a boolean when supplied.",
        {"allow_predicate_sink": False},
        ValueError,
        "mbarrier_state_token_form must be register, register_or_sink, or sink",
    ),
    _OrderCase(
        "mbarrier form before availability parsing",
        _raw(
            "mbarrier_state_token",
            mbarrier_state_token_form="invalid",
            sink_availability=[],
        ),
        ValueError,
        "mbarrier_state_token_form must be register, register_or_sink, or sink",
        {"mbarrier_state_token_form": "register"},
        TypeError,
        "availability must be an object",
    ),
    _OrderCase(
        "availability parsing before mbarrier applicability",
        _raw(sink_availability=[]),
        TypeError,
        "availability must be an object",
        {"sink_availability": {}},
        ValueError,
        "mbarrier state-token sink settings are only valid for kind 'mbarrier_state_token'",
    ),
    _OrderCase(
        "mbarrier applicability before scalar sink destination",
        _raw("reg_or_sink", role="src", access="read", sink_availability={}),
        ValueError,
        "mbarrier state-token sink settings are only valid for kind 'mbarrier_state_token'",
        {"sink_availability": _REMOVE},
        ValueError,
        "reg_or_sink must be a write destination",
    ),
    _OrderCase(
        "mbarrier destination before required availability",
        _raw(
            "mbarrier_state_token",
            mbarrier_state_token_form="sink",
            role="src",
            access="read",
        ),
        ValueError,
        "sink-capable mbarrier state token must be a write destination",
        {"role": "dst", "access": "write"},
        ValueError,
        "sink-capable mbarrier state token requires sink_availability",
    ),
    _OrderCase(
        "scalar sink destination before type tag",
        _raw("reg_or_sink", role="src", access="read", type_tag="tensor_map"),
        ValueError,
        "reg_or_sink must be a write destination",
        {"role": "dst", "access": "write"},
        ValueError,
        "type_tag is only valid for descriptor or typed_token operands",
    ),
    _OrderCase(
        "type tag before brace pack",
        _raw("tensor_coordinate", type_tag="tensor_map"),
        ValueError,
        "type_tag is only valid for descriptor or typed_token operands",
        {"type_tag": _REMOVE},
        ValueError,
        "tensor_coordinate operand requires cardinality",
    ),
    _OrderCase(
        "cardinality bounds before element kinds",
        _raw("tensor_coordinate", cardinality={"min": 0, "max": 5}, element_kinds=[]),
        ValueError,
        "tensor_coordinate cardinality must be within 1..5 with min <= max",
        {"cardinality": {"min": 1, "max": 5}},
        ValueError,
        "tensor_coordinate element_kinds must be ('reg', 'imm')",
    ),
    _OrderCase(
        "brace pack metadata before vector configuration",
        _raw("reg_vector", cardinality={}, vector={}),
        ValueError,
        "cardinality and element_kinds are only valid for brace-pack primitives",
        {"cardinality": _REMOVE},
        ValueError,
        "reg_vector operand must declare vector.arity",
    ),
    _OrderCase(
        "vector arity before vector type policy",
        _raw("reg_vector", vector={"arity": 9, "type_policy": "invalid"}),
        ValueError,
        "resolved vector operands support at most eight elements",
        {"vector": {"arity": 4, "type_policy": "invalid"}},
        ValueError,
        "operand 'x': unsupported vector.type_policy 'invalid'",
    ),
    _OrderCase(
        "vector policy before sink boolean",
        _raw(
            "reg_vector", vector={"arity": 4, "type_policy": "invalid", "allow_sink": 1}
        ),
        ValueError,
        "operand 'x': unsupported vector.type_policy 'invalid'",
        {"vector": {"arity": 4, "type_policy": "aggregate", "allow_sink": 1}},
        TypeError,
        "reg_vector vector.allow_sink must be a boolean when supplied.",
    ),
    _OrderCase(
        "vector sink boolean before payload width",
        _raw(
            "reg_vector", vector={"arity": 4, "allow_sink": 1, "sink_payload_bits": 7}
        ),
        TypeError,
        "reg_vector vector.allow_sink must be a boolean when supplied.",
        {"vector": {"arity": 4, "allow_sink": False, "sink_payload_bits": 7}},
        ValueError,
        "reg_vector vector.sink_payload_bits must be an 8..256 multiple of eight.",
    ),
    _OrderCase(
        "payload width before sink dependency",
        _raw(
            "reg_vector",
            vector={"arity": 4, "allow_sink": False, "sink_payload_bits": 7},
        ),
        ValueError,
        "reg_vector vector.sink_payload_bits must be an 8..256 multiple of eight.",
        {"vector": {"arity": 4, "allow_sink": False, "sink_payload_bits": 8}},
        ValueError,
        "reg_vector vector.sink_payload_bits requires vector.allow_sink.",
    ),
    _OrderCase(
        "vector configuration before type expression",
        _raw("reg_vector", vector={}, type={"expr": 1}),
        ValueError,
        "reg_vector operand must declare vector.arity",
        {"vector": {"arity": 4}},
        TypeError,
        "type expression must be a string",
    ),
    _OrderCase(
        "type expression before register width",
        _raw(type={"expr": 1}, register_width="invalid"),
        TypeError,
        "type expression must be a string",
        {"type": "b32"},
        ValueError,
        "operand 'x': unsupported register_width 'invalid'",
    ),
    _OrderCase(
        "register width kind before missing type",
        _raw("imm", register_width="equal_or_wider"),
        ValueError,
        "operand 'x': equal_or_wider register_width is only valid for kind 'reg' or 'reg_vector'",
        {"kind": "reg"},
        ValueError,
        "operand 'x': equal_or_wider register_width requires a type expression",
    ),
    _OrderCase(
        "register width dependency before immediate conversion",
        _raw(register_width="equal_or_wider", immediate_conversion="invalid"),
        ValueError,
        "operand 'x': equal_or_wider register_width requires a type expression",
        {"type": "b32"},
        ValueError,
        "operand 'x': unsupported immediate_conversion 'invalid'",
    ),
    _OrderCase(
        "immediate conversion before address parsing",
        _raw(immediate_conversion="invalid", state_space=[]),
        ValueError,
        "operand 'x': unsupported immediate_conversion 'invalid'",
        {"immediate_conversion": "narrow"},
        ValueError,
        "operand state_space list must not be empty",
    ),
    _OrderCase(
        "state space parsing before parameter parsing",
        _raw("addr", state_space=[], parameter=[]),
        ValueError,
        "operand state_space list must not be empty",
        {"state_space": "global"},
        TypeError,
        "operand parameter constraint must be an object",
    ),
    _OrderCase(
        "parameter parsing before address applicability",
        _raw(state_space="global", parameter=[]),
        TypeError,
        "operand parameter constraint must be an object",
        {"parameter": _REMOVE},
        ValueError,
        "operand 'x': address constraints are only valid for kind 'addr' or 'cluster_address'",
    ),
    _OrderCase(
        "address applicability before parameter dependency",
        _raw(parameter={"direction": "input", "function_availability": {}}),
        ValueError,
        "operand 'x': address constraints are only valid for kind 'addr' or 'cluster_address'",
        {"kind": "addr"},
        ValueError,
        "operand 'x': parameter constraint requires a state_space modifier expression",
    ),
)


class OperandValidationOrderTests(unittest.TestCase):
    def assert_error(
        self, raw: dict[str, Any], exception: type[Exception], message: str
    ) -> None:
        with self.assertRaises(exception) as caught:
            normalize_operand(raw)
        self.assertIs(type(caught.exception), exception)
        self.assertEqual(str(caught.exception), message)

    def test_first_error_and_error_after_repair(self) -> None:
        for case in _CASES:
            with self.subTest(case=case.name):
                original = deepcopy(case.raw)
                self.assert_error(original, case.first_type, case.first_message)
                self.assertEqual(original, case.raw)

                repaired = deepcopy(case.raw)
                for field, value in case.repair.items():
                    if value is _REMOVE:
                        del repaired[field]
                    else:
                        repaired[field] = deepcopy(value)
                before_call = deepcopy(repaired)
                self.assert_error(repaired, case.second_type, case.second_message)
                self.assertEqual(repaired, before_call)


if __name__ == "__main__":
    unittest.main()
