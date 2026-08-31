from dataclasses import FrozenInstanceError
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import yaml

from code_gen import cpp_backend
from code_gen.cpp_backend import (
    CppDomain,
    configure_cpp_backend,
    cpp_value,
    load_cpp_backend,
)
from code_gen.model import (
    CodegenUnit,
    DomainBackend,
    EmitAlternativeBackend,
    EmitBackend,
    InstructionBackend,
    ModifierBackend,
    OperandBackend,
    RuntimeLookupKind,
)
from ir.resolved_ir import ResolvedField, ResolvedFieldOrigin, ResolvedFieldStorage


REPOSITORY_CPP_BACKEND_SPEC = (
    Path(__file__).resolve().parents[3]
    / "instructions/ptx_cpp_backend_spec/ptx_frontend.yaml"
)


class BackendModelTests(unittest.TestCase):
    def setUp(self) -> None:
        configure_cpp_backend(REPOSITORY_CPP_BACKEND_SPEC)

    def test_constructs_detached_cpp_backend_model(self) -> None:
        scalar_types = DomainBackend(
            cpp_type="ScalarType",
            values={"u32": "ScalarType::U32"},
        )
        emit = EmitBackend(
            kind="sub_variant",
            instance="data",
            type="Data",
            alternatives=(
                EmitAlternativeBackend(
                    name="IntegerData",
                    variants=("add_integer",),
                ),
            ),
        )
        add = InstructionBackend(
            opcode="add",
            cpp="Add",
            emit=emit,
            modifiers={
                "type": ModifierBackend(
                    field="type_",
                    cpp_type="ScalarType",
                    domain=CppDomain.SCALAR_TYPES.value,
                )
            },
            operands={"dst": OperandBackend(field="dst", cpp_type="Operand")},
            type_checker_rule="integer_arith::check_add",
            visitor_name="visitAdd",
            modifier_order=("type",),
            operand_order=("dst",),
        )

        unit = CodegenUnit(
            spec_schema="ptx-instr/v1",
            backend_schema="ptx-cpp-backend/v1",
            category="integer_arithmetic",
            namespace="ptx_frontend::generated",
            includes=("<variant>",),
            instructions=(),
            backends={"add": add},
            domains={CppDomain.SCALAR_TYPES.value: scalar_types},
        )

        self.assertEqual(unit.backends["add"].emit.alternatives[0].name, "IntegerData")
        self.assertEqual(
            unit.domains[CppDomain.SCALAR_TYPES.value].values["u32"],
            "ScalarType::U32",
        )

    def test_backend_model_is_frozen(self) -> None:
        emit = EmitBackend(kind="direct")

        with self.assertRaises(FrozenInstanceError):
            emit.kind = "sub_struct"  # type: ignore[misc]

    def test_loads_repository_cpp_emit_domains(self) -> None:
        unit = load_cpp_backend(REPOSITORY_CPP_BACKEND_SPEC)

        self.assertEqual(unit.backend_schema, "ptx-cpp-backend/v1")
        self.assertEqual(unit.spec_schema, "ptx-instr/v1")
        self.assertEqual(unit.backends, {})
        self.assertEqual(
            unit.domains[CppDomain.SCALAR_TYPES.value].values["f32"],
            "ScalarType::F32",
        )
        self.assertEqual(
            unit.domains[CppDomain.RESOLVED_OPERAND_ROLES.value].values[
                "Source"
            ],
            "check_end::OperandRole::Source",
        )
        self.assertEqual(
            unit.domains[CppDomain.CACHE_OPERATORS.value].values["ca"],
            "CacheOperator::Ca",
        )
        self.assertEqual(
            unit.domains[CppDomain.CACHE_OPERATORS.value].default,
            "CacheOperator::Unspecified",
        )
        self.assertEqual(
            unit.domains[CppDomain.EVICTION_PRIORITIES.value].values["no_allocate"],
            "EvictionPriority::NoAllocate",
        )
        self.assertEqual(
            unit.domains[CppDomain.EVICTION_PRIORITIES.value].values["evict_normal"],
            "EvictionPriority::EvictNormal",
        )
        self.assertEqual(
            unit.domains[CppDomain.COMPARISON_OPERATORS.value].values,
            {
                "eq": "ComparisonOperator::Eq",
                "lt": "ComparisonOperator::Lt",
                "ge": "ComparisonOperator::Ge",
            },
        )
        self.assertIs(
            unit.domains[CppDomain.COMPARISON_OPERATORS.value].runtime_lookup,
            RuntimeLookupKind.PTX_SUFFIX,
        )
        self.assertEqual(
            unit.domains[CppDomain.ROUNDING_MODES.value].values,
            {
                "rn": "RoundingMode::Rn",
                "rz": "RoundingMode::Rz",
                "rm": "RoundingMode::Rm",
                "rp": "RoundingMode::Rp",
                "rzi": "RoundingMode::Rzi",
            },
        )
        self.assertEqual(
            unit.domains[CppDomain.BOOLEAN_OPERATORS.value].values["xor"],
            "BooleanOperator::Xor",
        )
        modifier_types = unit.domains[
            CppDomain.MODIFIER_VALUE_CPP_TYPES.value
        ].values
        self.assertEqual(
            {
                kind: modifier_types[kind]
                for kind in ("type", "rounding", "comparison", "boolean_op")
            },
            {
                "type": "ScalarType",
                "rounding": "RoundingMode",
                "comparison": "ComparisonOperator",
                "boolean_op": "BooleanOperator",
            },
        )
        self.assertEqual(
            unit.domains[CppDomain.MEMORY_CONSISTENCIES.value].values["weak"],
            "MemoryConsistency::Weak",
        )
        self.assertEqual(
            unit.domains[CppDomain.MEMORY_CONSISTENCIES.value].values["acq_rel"],
            "MemoryConsistency::AcqRel",
        )
        self.assertEqual(
            unit.domains[CppDomain.MEMORY_SCOPES.value].default,
            "MemoryScope::None",
        )
        self.assertEqual(
            unit.domains[CppDomain.VECTOR_ARITIES.value].values["v8"],
            "VectorArity::V8",
        )
        self.assertEqual(
            unit.domains[CppDomain.RESOLVED_VALUE_CPP_TYPES.value].values[
                "MemoryConsistency"
            ],
            "MemoryConsistency",
        )
        self.assertEqual(
            unit.domains[CppDomain.RESOLVED_VALUE_CPP_TYPES.value].values[
                "MemoryScope"
            ],
            "MemoryScope",
        )
        self.assertEqual(
            unit.domains[CppDomain.REGISTER_WIDTH_POLICIES.value].values[
                "equal_or_wider"
            ],
            "base::ScalarTypeSizePolicy::EqualOrWider",
        )
        self.assertIs(
            unit.domains[CppDomain.SCALAR_TYPES.value].runtime_lookup,
            RuntimeLookupKind.PTX_SUFFIX,
        )
        self.assertIsNone(
            unit.domains[CppDomain.RESOLVED_OPERAND_ROLES.value].runtime_lookup
        )
        self.assertEqual(
            set(unit.domains), {domain.value for domain in CppDomain}
        )

    def test_model_emission_reads_cpp_spelling_from_backend_yaml(self) -> None:
        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["domains"][CppDomain.SCALAR_TYPES.value]["values"]["f32"] = (
            "CustomType::F32"
        )

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(
                yaml.safe_dump(raw, sort_keys=False), encoding="utf-8"
            )
            configure_cpp_backend(backend_path)

            field = ResolvedField(
                name="type",
                value_cpp_type="ScalarType",
                origin=ResolvedFieldOrigin.MODIFIER,
                source_name="type",
                storage=ResolvedFieldStorage.STATIC_CONSTANT,
                constant_value="f32",
            )
            self.assertEqual(field.cpp_constant_expr, "CustomType::F32")

    def test_reports_missing_cpp_domain_value(self) -> None:
        with self.assertRaisesRegex(ValueError, "has no value 'missing'"):
            cpp_value(CppDomain.SCALAR_TYPES, "missing")

    def test_requires_explicit_cpp_backend_configuration(self) -> None:
        with patch.object(cpp_backend, "_active_backend_spec", None):
            with self.assertRaisesRegex(RuntimeError, "C\\+\\+ backend is not configured"):
                cpp_backend.get_cpp_backend()

    def test_cpp_lookup_rejects_string_domain_identifiers(self) -> None:
        with self.assertRaisesRegex(TypeError, "CppDomain member"):
            cpp_value("scalar_types", "f32")  # type: ignore[arg-type]

    def test_rejects_missing_required_cpp_domain(self) -> None:
        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        del raw["domains"][CppDomain.SYNTAX_OPERAND_SHAPES.value]

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(
                yaml.safe_dump(raw, sort_keys=False), encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "missing required domains"):
                load_cpp_backend(backend_path)


if __name__ == "__main__":
    unittest.main()
