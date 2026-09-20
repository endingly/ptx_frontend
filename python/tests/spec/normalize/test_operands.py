"""Behavioral coverage for the public operand normalizer.

These tests deliberately import the spec implementation, not the legacy
code_gen copy. They test normalized results and diagnostics, not the private
helper layout. JSON-schema validation and generated C++ belong in other suites.
"""

from copy import deepcopy
from dataclasses import asdict
from itertools import product
from typing import Any
import unittest

from ptx_frontend.spec.model import (
    MbarrierStateTokenForm,
    OperandImmediateConversionPolicy,
    OperandAccess,
    OperandKind,
    OperandParameterConstraint,
    OperandRegisterWidthPolicy,
    OperandSpec,
    OperandStateSpaceExpression,
    OperandStateSpaceValue,
    OperandTypeExpression,
    OperandTypeExpressionKind,
    OperandVectorArityExpression,
    OperandVectorTypePolicy,
    OperandRole,
)
from ptx_frontend.spec.normalize.operands import normalize_operand


def _operand(kind: str = "reg", **fields: Any) -> dict[str, Any]:
    return {"name": "x", "kind": kind, **fields}


def _vector(kind: str = "reg_vector", **options: Any) -> dict[str, Any]:
    return _operand(kind, vector={"arity": 4, **options})


def _pack(kind: str = "tensor_coordinate", **fields: Any) -> dict[str, Any]:
    return _operand(
        kind,
        **{
            "cardinality": {"min": 1, "max": 5 if kind == "tensor_coordinate" else 64},
            "element_kinds": ["reg", "imm"] if kind == "tensor_coordinate" else ["reg"],
            **fields,
        },
    )


class OperandNormalizationTests(unittest.TestCase):
    def assert_rejected(
        self, raw: dict[str, Any], exception: type[Exception], message: str
    ) -> Exception:
        """Check the exact diagnostic without relying on regex punctuation."""
        with self.assertRaises(exception) as caught:
            normalize_operand(raw)
        self.assertIs(type(caught.exception), exception)
        self.assertEqual(str(caught.exception), message)
        return caught.exception

    def test_minimal_operand_preserves_every_default(self) -> None:
        operand = normalize_operand(_operand())
        self.assertIs(type(operand), OperandSpec)
        self.assertEqual(
            asdict(operand),
            {
                "name": "x",
                "kind": OperandKind.REGISTER,
                "role": None,
                "access": None,
                "type_expression": None,
                "register_width_policy": OperandRegisterWidthPolicy.SAME_WIDTH,
                "immediate_conversion_policy": OperandImmediateConversionPolicy.NARROW,
                "state_space_values": (),
                "state_space_expression": None,
                "parameter_constraint": None,
                "vector_arities": (),
                "vector_arity_expression": None,
                "vector_type_policy": OperandVectorTypePolicy.AGGREGATE,
                "vector_allow_sink": False,
                "vector_sink_payload_bits": 0,
                "allow_destination_sink": False,
                "allow_predicate_sink": False,
                "mbarrier_state_token_form": MbarrierStateTokenForm.REGISTER,
                "sink_availability": {},
                "type_tag": None,
                "minimum_elements": None,
                "maximum_elements": None,
                "element_kinds": (),
            },
        )
        self.assertIs(
            operand.register_width_policy, OperandRegisterWidthPolicy.SAME_WIDTH
        )
        self.assertIs(
            operand.immediate_conversion_policy, OperandImmediateConversionPolicy.NARROW
        )
        self.assertIs(operand.vector_type_policy, OperandVectorTypePolicy.AGGREGATE)
        self.assertIs(
            operand.mbarrier_state_token_form, MbarrierStateTokenForm.REGISTER
        )

    def test_identity_role_access_and_fixed_type_are_preserved(self) -> None:
        operand = normalize_operand(
            {
                "name": "dst",
                "kind": "reg",
                "role": "dst",
                "access": "write",
                "type": "b32",
            }
        )
        self.assertEqual(
            operand,
            OperandSpec(
                name="dst",
                kind=OperandKind.REGISTER,
                role=OperandRole.DESTINATION,
                access=OperandAccess.WRITE,
                type_expression=OperandTypeExpression(
                    OperandTypeExpressionKind.FIXED_SCALAR, scalar_type="b32"
                ),
            ),
        )

    def test_shfl_sink_flags_are_independent(self) -> None:
        for destination, predicate in product((False, True), repeat=2):
            with self.subTest(destination=destination, predicate=predicate):
                operand = normalize_operand(
                    _operand(
                        "shfl_dest",
                        allow_destination_sink=destination,
                        allow_predicate_sink=predicate,
                    )
                )
                self.assertIs(operand.allow_destination_sink, destination)
                self.assertIs(operand.allow_predicate_sink, predicate)

    def test_shfl_sink_flags_reject_non_booleans(self) -> None:
        for field, value in product(
            ("allow_destination_sink", "allow_predicate_sink"),
            (None, 0, 1, "false", []),
        ):
            with self.subTest(field=field, value=value):
                self.assert_rejected(
                    _operand("shfl_dest", **{field: value}),
                    TypeError,
                    f"{field} must be a boolean when supplied.",
                )

    def test_shfl_sink_flag_presence_is_restricted_even_when_false(self) -> None:
        for field in ("allow_destination_sink", "allow_predicate_sink"):
            with self.subTest(field=field):
                raw = _operand()
                raw[field] = False

                self.assert_rejected(
                    raw,
                    ValueError,
                    f"{field} is only valid for kind 'shfl_dest'",
                )

    def test_mbarrier_register_form_defaults(self) -> None:
        for fields in (
            {},
            {"mbarrier_state_token_form": "register", "sink_availability": {}},
        ):
            with self.subTest(fields=fields):
                operand = normalize_operand(_operand("mbarrier_state_token", **fields))
                self.assertIs(
                    operand.mbarrier_state_token_form, MbarrierStateTokenForm.REGISTER
                )
                self.assertEqual(operand.sink_availability, {})

    def test_mbarrier_sink_forms_preserve_availability(self) -> None:
        availabilities = (
            {"ptx": "8.0", "sm": 90},
            {"any_of": [{"sm": 90}, {"target": "sm_100a"}]},
        )
        for form, availability in product(("register_or_sink", "sink"), availabilities):
            with self.subTest(form=form, availability=availability):
                operand = normalize_operand(
                    _operand(
                        "mbarrier_state_token",
                        role="dst",
                        access="write",
                        mbarrier_state_token_form=form,
                        sink_availability=availability,
                    )
                )
                self.assertIs(
                    operand.mbarrier_state_token_form, MbarrierStateTokenForm(form)
                )
                self.assertEqual(operand.sink_availability, availability)

    def test_mbarrier_settings_are_restricted_by_presence(self) -> None:
        for field, value in (
            ("mbarrier_state_token_form", "register"),
            ("sink_availability", {}),
        ):
            with self.subTest(field=field):
                raw = _operand()
                raw[field] = value

                self.assert_rejected(
                    raw,
                    ValueError,
                    "mbarrier state-token sink settings are only valid for "
                    "kind 'mbarrier_state_token'",
                )

    def test_register_only_mbarrier_rejects_sink_availability(self) -> None:
        self.assert_rejected(
            _operand("mbarrier_state_token", sink_availability={"sm": 90}),
            ValueError,
            "register-only mbarrier state token cannot have sink_availability",
        )

    def test_sink_capable_mbarrier_requires_write_destination(self) -> None:
        for form, role, access in product(
            ("register_or_sink", "sink"), (None, "src", "dst"), (None, "read", "write")
        ):
            if (role, access) == ("dst", "write"):
                continue
            with self.subTest(form=form, role=role, access=access):
                self.assert_rejected(
                    _operand(
                        "mbarrier_state_token",
                        role=role,
                        access=access,
                        mbarrier_state_token_form=form,
                        sink_availability={"sm": 90},
                    ),
                    ValueError,
                    "sink-capable mbarrier state token must be a write destination",
                )

    def test_sink_capable_mbarrier_requires_nonempty_availability(self) -> None:
        for form, settings in product(
            ("register_or_sink", "sink"), ({}, {"sink_availability": {}})
        ):
            with self.subTest(form=form, settings=settings):
                self.assert_rejected(
                    _operand(
                        "mbarrier_state_token",
                        role="dst",
                        access="write",
                        mbarrier_state_token_form=form,
                        **settings,
                    ),
                    ValueError,
                    "sink-capable mbarrier state token requires sink_availability",
                )

    def test_scalar_sink_kinds_require_write_destinations(self) -> None:
        for kind in ("reg_or_sink", "pred_or_sink", "pred_pair_or_sink"):
            self.assertEqual(
                normalize_operand(_operand(kind, role="dst", access="write")),
                OperandSpec(
                    name="x",
                    kind=OperandKind(kind),
                    role=OperandRole.DESTINATION,
                    access=OperandAccess.WRITE,
                ),
            )
            for role, access in ((None, None), ("src", "write"), ("dst", "read")):
                with self.subTest(kind=kind, role=role, access=access):
                    self.assert_rejected(
                        _operand(kind, role=role, access=access),
                        ValueError,
                        f"{kind} must be a write destination",
                    )

    def test_descriptor_and_typed_token_preserve_type_tags(self) -> None:
        for kind in ("descriptor", "typed_token"):
            with self.subTest(kind=kind):
                self.assertEqual(
                    normalize_operand(
                        _operand(kind, type_tag="tensor_map_2d")
                    ).type_tag,
                    "tensor_map_2d",
                )

    def test_type_tag_constraints(self) -> None:
        for kind, tag in product(
            ("descriptor", "typed_token"), (None, "", "Upper", "two__words", "_a", 7)
        ):
            with self.subTest(kind=kind, tag=tag):
                self.assert_rejected(
                    _operand(kind, type_tag=tag),
                    ValueError,
                    f"{kind} operand requires a lower-snake type_tag",
                )
        self.assert_rejected(
            _operand(type_tag="tensor_map"),
            ValueError,
            "type_tag is only valid for descriptor or typed_token operands",
        )

    def test_brace_pack_cardinality_and_element_order(self) -> None:
        for kind, maximum, kinds in (
            ("tensor_coordinate", 5, ["reg", "imm"]),
            ("tensor_coordinate", 5, ["imm", "reg"]),
            ("matrix_fragment", 64, ["reg"]),
        ):
            with self.subTest(kind=kind, kinds=kinds):
                operand = normalize_operand(_pack(kind, element_kinds=kinds))
                self.assertEqual(operand.minimum_elements, 1)
                self.assertEqual(operand.maximum_elements, maximum)
                self.assertEqual(
                    operand.element_kinds,
                    tuple(OperandKind(value) for value in kinds),
                )
                exact = normalize_operand(
                    _pack(kind, cardinality={"min": maximum, "max": maximum})
                )
                self.assertEqual(
                    (exact.minimum_elements, exact.maximum_elements), (maximum, maximum)
                )

    def test_brace_pack_requires_cardinality_object(self) -> None:
        for kind, cardinality in product(
            ("tensor_coordinate", "matrix_fragment"), (None, [], 4)
        ):
            with self.subTest(kind=kind, cardinality=cardinality):
                self.assert_rejected(
                    _pack(kind, cardinality=cardinality),
                    ValueError,
                    f"{kind} operand requires cardinality",
                )

    def test_brace_pack_cardinality_boundaries_and_boolean_rejection(self) -> None:
        for kind, ceiling in (("tensor_coordinate", 5), ("matrix_fragment", 64)):
            for minimum, maximum in (
                (0, 1),
                (2, 1),
                (1, ceiling + 1),
                (True, 1),
                (1, True),
                (1.0, 2),
                (None, 2),
                (1, None),
            ):
                with self.subTest(kind=kind, minimum=minimum, maximum=maximum):
                    self.assert_rejected(
                        _pack(kind, cardinality={"min": minimum, "max": maximum}),
                        ValueError,
                        f"{kind} cardinality must be within 1..{ceiling} with min <= max",
                    )

    def test_brace_pack_element_kinds_require_exact_members_without_duplicates(
        self,
    ) -> None:
        for kind, expected, invalid in (
            (
                "tensor_coordinate",
                ("reg", "imm"),
                ([], ["reg"], ["reg", "reg"], ["reg", "imm", "reg"]),
            ),
            (
                "matrix_fragment",
                ("reg",),
                ([], ["imm"], ["reg", "imm"], ["reg", "reg"]),
            ),
        ):
            for kinds in invalid:
                with self.subTest(kind=kind, kinds=kinds):
                    self.assert_rejected(
                        _pack(kind, element_kinds=kinds),
                        ValueError,
                        f"{kind} element_kinds must be {expected!r}",
                    )
            for kinds in (None, expected):
                with self.subTest(kind=kind, non_list=kinds):
                    self.assert_rejected(
                        _pack(kind, element_kinds=kinds),
                        ValueError,
                        f"{kind} operand requires element_kinds",
                    )

    def test_non_pack_metadata_distinguishes_null_from_supplied_values(self) -> None:
        for field in ("cardinality", "element_kinds"):
            with self.subTest(field=field):
                raw = _operand()
                raw[field] = None

                # None behaves as absent.
                normalize_operand(raw)

                raw[field] = {}
                self.assert_rejected(
                    raw,
                    ValueError,
                    "cardinality and element_kinds are only valid for "
                    "brace-pack primitives",
                )

    def test_vector_arities_support_scalar_list_and_modifier_forms(self) -> None:
        for kind in ("reg_vector", "vector_reg", "vector_sreg"):
            for arity, expected, expression in (
                (4, (4,), None),
                ([8, 2, 4], (8, 2, 4), None),
                ({"expr": "modifier(vec)"}, (), OperandVectorArityExpression("vec")),
            ):
                with self.subTest(kind=kind, arity=arity):
                    operand = normalize_operand(_vector(kind, arity=arity))
                    self.assertEqual(operand.vector_arities, expected)
                    self.assertEqual(operand.vector_arity_expression, expression)
                    self.assertIs(
                        operand.vector_type_policy, OperandVectorTypePolicy.AGGREGATE
                    )
                    self.assertFalse(operand.vector_allow_sink)
                    self.assertEqual(operand.vector_sink_payload_bits, 0)

    def test_vectors_require_arity(self) -> None:
        for kind, vector in product(
            ("reg_vector", "vector_reg", "vector_sreg"), (None, {}, [])
        ):
            with self.subTest(kind=kind, vector=vector):
                self.assert_rejected(
                    _operand(kind, vector=vector),
                    ValueError,
                    f"{kind} operand must declare vector.arity",
                )

    def test_vector_arity_type_and_upper_bound(self) -> None:
        for value in (None, "4", 4.0, (2, 4)):
            with self.subTest(value=value):
                self.assert_rejected(
                    _vector(arity=value),
                    TypeError,
                    "vector.arity must be an integer, list, or expression",
                )
        for value in (9, [2, 9]):
            with self.subTest(value=value):
                self.assert_rejected(
                    _vector(arity=value),
                    ValueError,
                    "resolved vector operands support at most eight elements",
                )

    def test_vector_arity_expression_diagnostics(self) -> None:
        cases = (
            ({}, ValueError, "vector arity object must be a modifier expression"),
            ({"expr": 4}, TypeError, "vector arity expression must be a string"),
            (
                {"expr": "other(v)"},
                ValueError,
                "unsupported vector arity expression; use modifier(<modifier_name>)",
            ),
        )
        for arity, exception, message in cases:
            with self.subTest(arity=arity):
                self.assert_rejected(_vector(arity=arity), exception, message)

    def test_vector_policy_and_sink_fields_survive_assembly(self) -> None:
        for policy, bits in product(("aggregate", "element"), (8, 128, 256)):
            with self.subTest(policy=policy, bits=bits):
                operand = normalize_operand(
                    _vector(type_policy=policy, allow_sink=True, sink_payload_bits=bits)
                )
                self.assertIs(
                    operand.vector_type_policy, OperandVectorTypePolicy(policy)
                )
                self.assertTrue(operand.vector_allow_sink)
                self.assertEqual(operand.vector_sink_payload_bits, bits)

    def test_vector_allow_sink_requires_boolean(self) -> None:
        for value in (None, 0, 1, "true"):
            with self.subTest(value=value):
                self.assert_rejected(
                    _vector(allow_sink=value),
                    TypeError,
                    "reg_vector vector.allow_sink must be a boolean when supplied.",
                )

    def test_vector_sink_payload_validates_width_and_enabled_sink(self) -> None:
        for payload_bits in (0, 7, 9, 264, True):
            with self.subTest(payload_bits=payload_bits):
                raw = _operand(
                    "reg_vector",
                    vector={
                        "arity": [4],
                        "allow_sink": True,
                    },
                )
                raw["vector"]["sink_payload_bits"] = payload_bits

                self.assert_rejected(
                    raw,
                    ValueError,
                    "reg_vector vector.sink_payload_bits must be an 8..256 "
                    "multiple of eight.",
                )

        raw = _operand(
            "reg_vector",
            vector={
                "arity": [4],
            },
        )
        raw["vector"]["sink_payload_bits"] = 128

        self.assert_rejected(
            raw,
            ValueError,
            "reg_vector vector.sink_payload_bits requires vector.allow_sink.",
        )

    def test_modifier_type_expression_is_preserved(self) -> None:
        operand = normalize_operand(_operand(type={"expr": "modifier(type)"}))
        self.assertEqual(
            operand.type_expression,
            OperandTypeExpression(
                OperandTypeExpressionKind.MODIFIER, modifier_name="type"
            ),
        )

    def test_register_width_policies_are_typed(self) -> None:
        for policy in ("exact", "same_width", "equal_or_wider"):
            with self.subTest(policy=policy):
                operand = normalize_operand(_operand(type="b32", register_width=policy))
                self.assertIs(
                    operand.register_width_policy, OperandRegisterWidthPolicy(policy)
                )
        operand = normalize_operand(
            _vector()
            | {"type": {"expr": "modifier(type)"}, "register_width": "equal_or_wider"}
        )
        self.assertIs(
            operand.register_width_policy, OperandRegisterWidthPolicy.EQUAL_OR_WIDER
        )

    def test_equal_or_wider_requires_supported_kind_and_type(self) -> None:
        for kind in ("imm", "reg_or_imm", "vector_reg", "vector_sreg"):
            raw = _vector(kind) if kind.startswith("vector_") else _operand(kind)
            with self.subTest(kind=kind):
                self.assert_rejected(
                    raw | {"type": "b32", "register_width": "equal_or_wider"},
                    ValueError,
                    "operand 'x': equal_or_wider register_width is only valid for kind 'reg' or 'reg_vector'",
                )
        for raw in (_operand(), _vector()):
            with self.subTest(kind=raw["kind"]):
                self.assert_rejected(
                    raw | {"register_width": "equal_or_wider"},
                    ValueError,
                    "operand 'x': equal_or_wider register_width requires a type expression",
                )

    def test_immediate_conversion_is_typed_and_kind_restricted(self) -> None:
        for raw in (_operand("imm"), _operand("reg_or_imm"), _pack()):
            with self.subTest(kind=raw["kind"]):
                operand = normalize_operand(
                    raw | {"immediate_conversion": "require_target_range"}
                )
                self.assertIs(
                    operand.immediate_conversion_policy,
                    OperandImmediateConversionPolicy.REQUIRE_TARGET_RANGE,
                )
        self.assert_rejected(
            _operand(immediate_conversion="require_target_range"),
            ValueError,
            "operand 'x': require_target_range immediate_conversion requires an immediate-capable operand",
        )

    def test_enum_errors_preserve_explicit_exception_causes(self) -> None:
        for raw, message, enum_name in (
            (
                _operand("mbarrier_state_token", mbarrier_state_token_form="invalid"),
                "mbarrier_state_token_form must be register, register_or_sink, or sink",
                "MbarrierStateTokenForm",
            ),
            (
                _vector(type_policy="invalid"),
                "operand 'x': unsupported vector.type_policy 'invalid'",
                "OperandVectorTypePolicy",
            ),
            (
                _operand(register_width="invalid"),
                "operand 'x': unsupported register_width 'invalid'",
                "OperandRegisterWidthPolicy",
            ),
            (
                _operand(immediate_conversion="invalid"),
                "operand 'x': unsupported immediate_conversion 'invalid'",
                "OperandImmediateConversionPolicy",
            ),
        ):
            with self.subTest(enum=enum_name):
                error = self.assert_rejected(raw, ValueError, message)
                self.assertIs(type(error.__cause__), ValueError)
                self.assertIn(enum_name, str(error.__cause__))
                self.assertTrue(error.__suppress_context__)

    def test_static_address_spaces_preserve_order_and_availability(self) -> None:
        for kind in ("addr", "cluster_address"):
            with self.subTest(kind=kind):
                operand = normalize_operand(
                    _operand(
                        kind,
                        state_space=[
                            "global",
                            {"value": "shared", "availability": {"sm": 90}},
                        ],
                    )
                )
                self.assertEqual(
                    operand.state_space_values,
                    (
                        OperandStateSpaceValue("global"),
                        OperandStateSpaceValue("shared", {"sm": 90}),
                    ),
                )
                self.assertIsNone(operand.state_space_expression)
                self.assertIsNone(operand.parameter_constraint)
                self.assertEqual(
                    normalize_operand(
                        _operand(kind, state_space="global")
                    ).state_space_values,
                    (OperandStateSpaceValue("global"),),
                )

    def test_dynamic_address_and_parameter_constraints_survive_assembly(self) -> None:
        for direction in ("input", "return"):
            with self.subTest(direction=direction):
                operand = normalize_operand(
                    _operand(
                        "addr",
                        state_space={"expr": "modifier(space)"},
                        parameter={
                            "direction": direction,
                            "function_availability": {"sm": 20},
                        },
                    )
                )
                self.assertEqual(operand.state_space_values, ())
                self.assertEqual(
                    operand.state_space_expression, OperandStateSpaceExpression("space")
                )
                self.assertEqual(
                    operand.parameter_constraint,
                    OperandParameterConstraint(direction, {"sm": 20}),
                )

    def test_address_constraints_require_address_kind(self) -> None:
        for field, value in (
            ("state_space", "global"),
            (
                "parameter",
                {
                    "direction": "input",
                    "function_availability": {"ptx": "1.0"},
                },
            ),
        ):
            with self.subTest(field=field):
                raw = _operand()
                raw[field] = value

                self.assert_rejected(
                    raw,
                    ValueError,
                    "operand 'x': address constraints are only valid for "
                    "kind 'addr' or 'cluster_address'",
                )

    def test_parameter_requires_dynamic_state_space(self) -> None:
        for fields in ({}, {"state_space": "param"}):
            with self.subTest(fields=fields):
                self.assert_rejected(
                    _operand(
                        "addr",
                        parameter={"direction": "input", "function_availability": {}},
                        **fields,
                    ),
                    ValueError,
                    "operand 'x': parameter constraint requires a state_space modifier expression",
                )

    def test_address_parser_errors_propagate(self) -> None:
        cases = (
            (
                {"state_space": []},
                ValueError,
                "operand state_space list must not be empty",
            ),
            (
                {"state_space": ["shared", "shared"]},
                ValueError,
                "duplicate operand state space 'shared'",
            ),
            (
                {"state_space": 3},
                TypeError,
                "operand state_space must be a value or expression",
            ),
            (
                {"parameter": []},
                TypeError,
                "operand parameter constraint must be an object",
            ),
            (
                {"parameter": {"direction": "other", "function_availability": {}}},
                ValueError,
                "unsupported parameter direction 'other'",
            ),
        )
        for fields, exception, message in cases:
            with self.subTest(fields=fields):
                self.assert_rejected(_operand("addr", **fields), exception, message)

    def test_success_does_not_mutate_input(self) -> None:
        inputs = (
            _vector(arity=[8, 2, 4], allow_sink=True, sink_payload_bits=128),
            _pack(element_kinds=["imm", "reg"]),
            _operand(
                "mbarrier_state_token",
                role="dst",
                access="write",
                mbarrier_state_token_form="sink",
                sink_availability={"any_of": [{"sm": 90}, {"target": "sm_100a"}]},
            ),
            _operand(
                "addr",
                state_space={"expr": "modifier(space)"},
                parameter={
                    "direction": "input",
                    "function_availability": {"any_of": [{"sm": 20}]},
                },
            ),
        )
        for raw in inputs:
            with self.subTest(kind=raw["kind"]):
                original = deepcopy(raw)
                first = normalize_operand(raw)
                second = normalize_operand(raw)
                self.assertEqual(raw, original)
                self.assertEqual(first, second)

    def test_failure_does_not_mutate_input(self) -> None:
        raw = _vector(arity=[2, 4], allow_sink=False, sink_payload_bits=128)
        original = deepcopy(raw)
        self.assert_rejected(
            raw,
            ValueError,
            "reg_vector vector.sink_payload_bits requires vector.allow_sink.",
        )
        self.assertEqual(raw, original)

    def test_default_availability_is_not_shared_between_results(self) -> None:
        first = normalize_operand(_operand())
        second = normalize_operand(_operand())
        self.assertIsNot(first.sink_availability, second.sink_availability)
        first.sink_availability["sm"] = 90
        self.assertEqual(second.sink_availability, {})


if __name__ == "__main__":
    unittest.main()
