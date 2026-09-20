from dataclasses import FrozenInstanceError
from pathlib import Path
from importlib.resources.abc import Traversable
import tempfile
import unittest

import yaml

from ptx_frontend.code_gen.resolved_field_names import (
    field_cpp_constant_expr,
    field_cpp_type,
    field_value_cpp_type,
)
from ptx_frontend.code_gen.emit.operand_views import emit_check_operand_view
from ptx_frontend.code_gen.cpp_backend import (
    CppDomain,
    cpp_value,
    load_cpp_backend,
)
from ptx_frontend.spec.model import (
    CodegenUnit,
    DomainBackend,
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
        self.backend = load_cpp_backend(REPOSITORY_CPP_BACKEND_SPEC)

    def test_loads_unhashable_resource_without_caching_direct_reads(self) -> None:
        """Direct loading accepts Traversable resources and rereads their data."""
        resource = UnhashableResource(REPOSITORY_CPP_BACKEND_SPEC)
        first = load_cpp_backend(resource)
        second = load_cpp_backend(resource)
        self.assertEqual(first, second)
        self.assertIsNot(first, second)
        self.assertEqual(resource.reads, 2)

    def test_direct_load_rereads_changed_resource_contents(self) -> None:
        """Each explicit load captures the resource contents at that call."""
        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "backend.yaml"
            path.write_text(yaml.safe_dump(raw), encoding="utf-8")
            resource = UnhashableResource(path)
            original = load_cpp_backend(resource)
            raw["domains"][CppDomain.SCALAR_TYPES.value]["values"]["f32"] = (
                "UpdatedScalarType::F32"
            )
            path.write_text(yaml.safe_dump(raw), encoding="utf-8")
            updated = load_cpp_backend(resource)
            self.assertNotEqual(
                original.domains[CppDomain.SCALAR_TYPES.value].values["f32"],
                "UpdatedScalarType::F32",
            )
            self.assertEqual(
                updated.domains[CppDomain.SCALAR_TYPES.value].values["f32"],
                "UpdatedScalarType::F32",
            )

    def test_constructs_detached_cpp_backend_model(self) -> None:
        scalar_types = DomainBackend(
            cpp_type="ScalarType",
            values={"u32": "ScalarType::U32"},
        )
        unit = CodegenUnit(
            spec_schema="ptx-instr/v1",
            backend_schema="ptx-cpp-backend/v2",
            domains={CppDomain.SCALAR_TYPES.value: scalar_types},
        )

        self.assertEqual(
            unit.domains[CppDomain.SCALAR_TYPES.value].values["u32"],
            "ScalarType::U32",
        )

    def test_backend_model_is_frozen(self) -> None:
        domain = DomainBackend(cpp_type="ScalarType", values={"u32": "ScalarType::U32"})

        with self.assertRaises(FrozenInstanceError):
            domain.cpp_type = "OtherScalarType"  # type: ignore[misc]

    def test_loads_repository_cpp_emit_domains(self) -> None:
        unit = load_cpp_backend(REPOSITORY_CPP_BACKEND_SPEC)

        self.assertEqual(unit.backend_schema, "ptx-cpp-backend/v2")
        self.assertEqual(unit.spec_schema, "ptx-instr/v1")
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
            backend = load_cpp_backend(backend_path)

            field = ResolvedField(
                name="type",
                value_kind=ResolvedValueKind.SCALAR_TYPE,
                origin=ResolvedFieldOrigin.MODIFIER,
                source_name="type",
                storage=ResolvedFieldStorage.STATIC_CONSTANT,
                constant_value="f32",
            )
            self.assertEqual(
                field_cpp_constant_expr(field, backend=backend), "CustomType::F32"
            )

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
            backend = load_cpp_backend(backend_path)

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
                field_value_cpp_type(field, backend=backend),
                "CustomScalarType",
            )
            self.assertEqual(
                field_cpp_type(field, backend=backend),
                "WithLocs<CustomScalarType>",
            )

    def test_operand_views_use_semantic_domain_mappings(self) -> None:
        """Semantic value and default mappings control all emitted equivalents."""

        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["domains"][CppDomain.SCALAR_TYPES.value]["default"] = "TestScalar::Invalid"
        raw["domains"][CppDomain.SCALAR_TYPES.value]["values"]["pred"] = "TestScalar::Pred"
        raw["domains"][CppDomain.PARAMETER_DIRECTIONS.value]["default"] = "TestDirection::None"
        raw["domains"][CppDomain.PARAMETER_DIRECTIONS.value]["values"].update(
            {"input": "TestDirection::Input", "return": "TestDirection::Return"}
        )
        raw["domains"][CppDomain.MEMORY_STATE_SPACES.value]["values"].update(
            {
                "global": "TestSpace::Global",
                "shared": "TestSpace::Shared",
                "local": "TestSpace::Local",
                "param": "TestSpace::Parameter",
                "const": "TestSpace::Constant",
            }
        )

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            backend = load_cpp_backend(backend_path)
            emitted = "\n".join(
                (
                    emit_check_operand_view(
                        ResolvedField(
                            name="predicate_pair",
                            value_kind=ResolvedValueKind.PREDICATE_PAIR_OR_SINK,
                            origin=ResolvedFieldOrigin.OPERAND,
                            source_name="predicate_pair",
                        ),
                        "instruction",
                        backend,
                    ),
                    emit_check_operand_view(
                        ResolvedField(
                            name="predicate_source",
                            value_kind=ResolvedValueKind.PREDICATE_SOURCE,
                            origin=ResolvedFieldOrigin.OPERAND,
                            source_name="predicate_source",
                        ),
                        "instruction",
                        backend,
                    ),
                    emit_check_operand_view(
                        ResolvedField(
                            name="address",
                            value_kind=ResolvedValueKind.ADDRESS,
                            origin=ResolvedFieldOrigin.OPERAND,
                            source_name="address",
                        ),
                        "instruction",
                        backend,
                    ),
                )
            )

        for spelling in (
            "TestScalar::Invalid",
            "TestScalar::Pred",
            "TestDirection::None",
            "TestDirection::Input",
            "TestDirection::Return",
            "TestSpace::Global",
            "TestSpace::Shared",
            "TestSpace::Local",
            "TestSpace::Parameter",
            "TestSpace::Constant",
        ):
            self.assertIn(spelling, emitted)
        for retired_spelling in (
            "ScalarType::Invalid",
            "ScalarType::Pred",
            "ParameterDirection::None",
            "ParameterDirection::Input",
            "ParameterDirection::Return",
            "MemoryStateSpace::Global",
            "MemoryStateSpace::Shared",
            "MemoryStateSpace::Local",
            "MemoryStateSpace::Parameter",
            "MemoryStateSpace::Constant",
        ):
            self.assertNotIn(retired_spelling, emitted)

    def test_reports_missing_cpp_domain_value(self) -> None:
        with self.assertRaisesRegex(ValueError, "has no value 'missing'"):
            cpp_value(CppDomain.SCALAR_TYPES, "missing", backend=self.backend)

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
            field_cpp_constant_expr(field, backend=self.backend)

    def test_cpp_lookup_rejects_string_domain_identifiers(self) -> None:
        with self.assertRaisesRegex(TypeError, "CppDomain member"):
            cpp_value("scalar_types", "f32", backend=self.backend)  # type: ignore[arg-type]

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

    def test_rejects_unknown_cpp_domain(self) -> None:
        """The v2 domain contract rejects mappings the generator would ignore."""

        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["domains"]["unused_domain"] = {
            "cpp_type": "Unused",
            "values": {"value": "Unused::Value"},
        }

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "unsupported domain 'unused_domain'"):
                load_cpp_backend(backend_path)

    def test_rejects_removed_backend_configuration_fields(self) -> None:
        """V2 declines retired layout controls and unused value annotations."""

        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["target"] = "all"

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "Additional properties are not allowed"):
                load_cpp_backend(backend_path)

        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["domains"][CppDomain.SCALAR_TYPES.value]["values"]["f32"] = {
            "cpp": "ScalarType::F32",
            "token": ".f32",
        }

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "is not valid under any of the given schemas"):
                load_cpp_backend(backend_path)

    def test_rejects_retired_v1_before_v2_schema_validation(self) -> None:
        """A v1 file receives a migration diagnostic instead of a v2 const error."""

        raw = yaml.safe_load(REPOSITORY_CPP_BACKEND_SPEC.read_text(encoding="utf-8"))
        raw["schema"] = "ptx-cpp-backend/v1"

        with tempfile.TemporaryDirectory() as directory:
            backend_path = Path(directory) / "backend.yaml"
            backend_path.write_text(yaml.safe_dump(raw, sort_keys=False), encoding="utf-8")
            with self.assertRaisesRegex(
                ValueError,
                "retired; migrate to 'ptx-cpp-backend/v2'",
            ):
                load_cpp_backend(backend_path)


if __name__ == "__main__":
    unittest.main()
