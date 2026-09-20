from dataclasses import FrozenInstanceError
from pathlib import Path
from importlib.resources.abc import Traversable
import tempfile
import unittest
from unittest.mock import patch

import yaml

from ptx_frontend.code_gen import cpp_backend
from ptx_frontend.code_gen.resolved_field_names import (
    field_cpp_constant_expr,
    field_cpp_type,
    field_value_cpp_type,
)
from ptx_frontend.code_gen.cpp_backend import (
    CppDomain,
    configure_cpp_backend,
    cpp_value,
    load_cpp_backend,
)
from ptx_frontend.spec.model import (
    CodegenUnit,
    DomainBackend,
    EmitAlternativeBackend,
    EmitBackend,
    InstructionBackend,
    ModifierBackend,
    OperandBackend,
    RuntimeLookupKind,
)
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedValueKind,
    from_instruction_spec,
)
from ptx_frontend.spec.resources import packaged_backend_spec

REPOSITORY_CPP_BACKEND_SPEC = packaged_backend_spec()


class UnhashableResource(Traversable):
    """Readable resource with no hash identity and observable resource reads."""

    __hash__ = None

    def __init__(self, path: Path) -> None:
        """Wrap a file whose contents can change between explicit loads."""
        self.path = path
        self.reads = 0

    @property
    def name(self) -> str:
        """Return the resource basename."""
        return self.path.name

    def iterdir(self):
        """Iterate wrapped children when this resource is a directory."""
        return (UnhashableResource(path) for path in self.path.iterdir())

    def is_dir(self) -> bool:
        """Report whether the resource is a directory."""
        return self.path.is_dir()

    def is_file(self) -> bool:
        """Report whether the resource is a file."""
        return self.path.is_file()

    def joinpath(self, *descendants: str):
        """Return a wrapped descendant resource."""
        return UnhashableResource(self.path.joinpath(*descendants))

    def open(self, mode="r", *args, **kwargs):
        """Open the resource and count reads requested by the loader."""
        self.reads += 1
        return self.path.open(mode, *args, **kwargs)


class BackendModelTests(unittest.TestCase):
    def setUp(self) -> None:
        configure_cpp_backend(REPOSITORY_CPP_BACKEND_SPEC)

    def test_loads_unhashable_resource_without_caching_direct_reads(self) -> None:
        """Direct loading accepts Traversable resources and rereads their data."""
        resource = UnhashableResource(REPOSITORY_CPP_BACKEND_SPEC)
        first = load_cpp_backend(resource)
        second = load_cpp_backend(resource)
        self.assertEqual(first, second)
        self.assertIsNot(first, second)
        self.assertEqual(resource.reads, 2)

    def test_configured_resource_cache_is_invalidated_on_configuration(self) -> None:
        """Repeated access caches a resource until even the same one is reset."""
        resource = UnhashableResource(REPOSITORY_CPP_BACKEND_SPEC)
        self.addCleanup(configure_cpp_backend, REPOSITORY_CPP_BACKEND_SPEC)
        configure_cpp_backend(resource)
        first = cpp_backend.get_cpp_backend()
        self.assertIs(cpp_backend.get_cpp_backend(), first)
        self.assertEqual(resource.reads, 1)
        configure_cpp_backend(resource)
        self.assertIsNot(cpp_backend.get_cpp_backend(), first)
        self.assertEqual(resource.reads, 2)

    def test_reconfiguration_reloads_changed_resource_contents(self) -> None:
        """Configuration fixes a snapshot while direct reads see file changes."""
        self.addCleanup(configure_cpp_backend, REPOSITORY_CPP_BACKEND_SPEC)
        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "backend.yaml"
            path.write_text(yaml.safe_dump(raw), encoding="utf-8")
            resource = UnhashableResource(path)
            configure_cpp_backend(resource)
            original = cpp_backend.get_cpp_backend()
            raw["namespace"] = "custom::updated"
            path.write_text(yaml.safe_dump(raw), encoding="utf-8")
            self.assertEqual(load_cpp_backend(resource).namespace, "custom::updated")
            self.assertIs(cpp_backend.get_cpp_backend(), original)
            configure_cpp_backend(resource)
            self.assertEqual(cpp_backend.get_cpp_backend().namespace, "custom::updated")

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
            unit.domains[CppDomain.RESOLVED_OPERAND_ROLES.value].values["Source"],
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
                "ne": "ComparisonOperator::Ne",
                "le": "ComparisonOperator::Le",
                "gt": "ComparisonOperator::Gt",
                "lo": "ComparisonOperator::Lo",
                "ls": "ComparisonOperator::Ls",
                "hi": "ComparisonOperator::Hi",
                "hs": "ComparisonOperator::Hs",
                "equ": "ComparisonOperator::Equ",
                "neu": "ComparisonOperator::Neu",
                "ltu": "ComparisonOperator::Ltu",
                "leu": "ComparisonOperator::Leu",
                "gtu": "ComparisonOperator::Gtu",
                "geu": "ComparisonOperator::Geu",
                "num": "ComparisonOperator::Num",
                "nan": "ComparisonOperator::Nan",
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
        modifier_types = unit.domains[CppDomain.MODIFIER_VALUE_CPP_TYPES.value].values
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
            unit.domains[CppDomain.ASYNC_PROXY_KINDS.value].values,
            {
                "async": "AsyncProxyKind::Async",
                "async.global": "AsyncProxyKind::AsyncGlobal",
                "async.shared::cta": "AsyncProxyKind::AsyncSharedCta",
                "async.shared::cluster": "AsyncProxyKind::AsyncSharedCluster",
            },
        )
        self.assertEqual(
            unit.domains[CppDomain.PROXY_KIND_PAIRS.value].values,
            {
                "tensormap::generic": "ProxyKindPair::TensormapToGeneric",
                "async::generic": "ProxyKindPair::AsyncToGeneric",
            },
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
        self.assertEqual(set(unit.domains), {domain.value for domain in CppDomain})

    def test_model_emission_reads_cpp_spelling_from_backend_yaml(self) -> None:
        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["domains"][CppDomain.SCALAR_TYPES.value]["values"][
            "f32"
        ] = "CustomType::F32"

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(
                yaml.safe_dump(raw, sort_keys=False), encoding="utf-8"
            )
            configure_cpp_backend(backend_path)

            field = ResolvedField(
                name="type",
                value_kind=ResolvedValueKind.SCALAR_TYPE,
                origin=ResolvedFieldOrigin.MODIFIER,
                source_name="type",
                storage=ResolvedFieldStorage.STATIC_CONSTANT,
                constant_value="f32",
            )
            self.assertEqual(field_cpp_constant_expr(field), "CustomType::F32")

    def test_resolved_value_kind_is_independent_of_cpp_type_spelling(self) -> None:
        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["domains"][CppDomain.RESOLVED_VALUE_CPP_TYPES.value]["values"][
            "ScalarType"
        ] = "CustomScalarType"

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(
                yaml.safe_dump(raw, sort_keys=False),
                encoding="utf-8",
            )
            configure_cpp_backend(backend_path)

            field = ResolvedField(
                name="type",
                value_kind=ResolvedValueKind.SCALAR_TYPE,
                origin=ResolvedFieldOrigin.MODIFIER,
                source_name="type",
            )

            self.assertIs(
                field.value_kind,
                ResolvedValueKind.SCALAR_TYPE,
            )
            self.assertEqual(
                field_value_cpp_type(field),
                "CustomScalarType",
            )
            self.assertEqual(
                field_cpp_type(field),
                "WithLocs<CustomScalarType>",
            )

    def test_reports_missing_cpp_domain_value(self) -> None:
        with self.assertRaisesRegex(ValueError, "has no value 'missing'"):
            cpp_value(CppDomain.SCALAR_TYPES, "missing")

    def test_valid_frontend_value_can_fail_only_at_codegen_capability(self) -> None:
        """PTX legality is broader than the configured C++ value map."""

        instruction = normalize_instruction_spec(
            {
                "category": "test",
                "codegen_category": "test",
                "instructions": [
                    {
                        "opcode": "sample",
                        "variants": [
                            {
                                "name": "sample_default",
                                "availability": {"ptx": "1.0"},
                                "modifiers": [
                                    {
                                        "name": "type",
                                        "kind": "type",
                                        "presence": "fixed",
                                        "value": "u4",
                                    }
                                ],
                                "operands": [],
                            }
                        ],
                    }
                ],
            }
        )[0]
        field = from_instruction_spec(instruction).variants[0].modifier_fields[0]

        with self.assertRaisesRegex(ValueError, "has no value 'u4'"):
            field_cpp_constant_expr(field)

    def test_requires_explicit_cpp_backend_configuration(self) -> None:
        with patch.object(cpp_backend, "_active_backend_spec", None):
            with self.assertRaisesRegex(
                RuntimeError, "C\\+\\+ backend is not configured"
            ):
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
