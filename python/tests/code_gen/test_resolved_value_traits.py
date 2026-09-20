import unittest
from types import SimpleNamespace

from ptx_frontend.code_gen.cpp_backend import (
    CppDomain,
    cpp_domain,
    load_cpp_backend,
)
from ptx_frontend.code_gen.resolved_value_traits import (
    RESOLVED_MODIFIER_VALUE_KINDS,
    modifier_default_cpp_expr,
    modifier_value_cpp_expr,
    resolved_modifier_value_traits,
)
from ptx_frontend.ir.resolved_ir import (
    ResolvedField,
    ResolvedFieldOrigin,
    ResolvedFieldStorage,
    ResolvedValueKind,
)
from ptx_frontend.ir.resolved_ir import (
    ResolvedModifierValueDomain,
    ResolvedModifierBinding,
    ResolvedModifierDefault,
    _build_modifier_default,
    _build_modifier_value_availability,
)
from ptx_frontend.ir.resolved_value_policy import (
    RESOLVED_MODIFIER_VALUE_KINDS as POLICY_MODIFIER_VALUE_KINDS,
    modifier_value_kind,
    resolved_modifier_value_policy,
)
from ptx_frontend.code_gen.model import ModifierSpec, ModifierValueSpec
from ptx_frontend.spec.model import ModifierKind, ModifierPresence
from ptx_frontend.spec.semantic_domains import (
    is_semantic_value,
    semantic_domain_for_modifier,
)
from ptx_frontend.spec.resources import packaged_backend_spec


class ResolvedValueTraitsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.backend = load_cpp_backend(packaged_backend_spec())

    def test_modifier_value_traits_cover_all_modifier_semantic_kinds(self) -> None:
        self.assertEqual(
            RESOLVED_MODIFIER_VALUE_KINDS,
            frozenset(
                {
                    ResolvedValueKind.BOOL,
                    ResolvedValueKind.SCALAR_TYPE,
                    ResolvedValueKind.ROUNDING_MODE,
                    ResolvedValueKind.COMPARISON_OPERATOR,
                    ResolvedValueKind.BOOLEAN_OPERATOR,
                    ResolvedValueKind.CACHE_OPERATOR,
                    ResolvedValueKind.EVICTION_PRIORITY,
                    ResolvedValueKind.PREFETCH_SIZE,
                    ResolvedValueKind.VECTOR_ARITY,
                    ResolvedValueKind.MEMORY_STATE_SPACE,
                    ResolvedValueKind.MEMORY_CONSISTENCY,
                    ResolvedValueKind.MEMORY_SCOPE,
                    ResolvedValueKind.MBARRIER_PHASE_TYPE,
                    ResolvedValueKind.MBARRIER_LAYOUT,
                    ResolvedValueKind.ASYNC_PROXY_KIND,
                    ResolvedValueKind.PROXY_KIND_PAIR,
                }
            ),
        )

    def test_traits_separate_semantic_kind_from_cpp_domain(self) -> None:
        traits = resolved_modifier_value_traits(ResolvedValueKind.SCALAR_TYPE)

        self.assertIs(
            traits.cpp_domain,
            CppDomain.SCALAR_TYPES,
        )
        self.assertEqual(
            traits.descriptor_member,
            "scalar_type",
        )
        self.assertIs(
            resolved_modifier_value_policy(ResolvedValueKind.SCALAR_TYPE).python_type,
            str,
        )
        self.assertEqual(
            RESOLVED_MODIFIER_VALUE_KINDS,
            POLICY_MODIFIER_VALUE_KINDS,
        )

    def test_bool_value_does_not_require_a_cpp_domain(self) -> None:
        traits = resolved_modifier_value_traits(ResolvedValueKind.BOOL)

        self.assertIsNone(traits.cpp_domain)
        self.assertEqual(
            modifier_value_cpp_expr(
                ResolvedValueKind.BOOL,
                True,
                backend=self.backend,
            ),
            "true",
        )

    def test_scalar_type_uses_backend_spelling(self) -> None:
        self.assertEqual(
            modifier_value_cpp_expr(
                ResolvedValueKind.SCALAR_TYPE,
                "f32",
                backend=self.backend,
            ),
            "ScalarType::F32",
        )

    def test_default_support_remains_restricted(self) -> None:
        with self.assertRaisesRegex(
            ValueError,
            "unsupported modifier default",
        ):
            modifier_default_cpp_expr(
                ResolvedValueKind.COMPARISON_OPERATOR,
                "eq",
                backend=self.backend,
            )

        with self.assertRaisesRegex(
            ValueError,
            "unsupported modifier default",
        ):
            modifier_default_cpp_expr(
                ResolvedValueKind.VECTOR_ARITY,
                "v4",
                backend=self.backend,
            )

        combined_type_diagnostics = {
            "semantics": "memory consistency",
            "scope": "memory scope",
            "phase_type": "mbarrier phase-type",
            "mbarrier_layout": "mbarrier layout",
            "proxy": "async proxy",
            "proxy_pair": "proxy pair",
        }
        for source_kind, label in combined_type_diagnostics.items():
            with self.assertRaisesRegex(
                ValueError,
                f"unsupported {label} value 1",
            ):
                _build_modifier_value_availability(
                    ModifierSpec(
                        name=source_kind,
                        kind=ModifierKind(source_kind),
                        presence=ModifierPresence.REQUIRED,
                    ),
                    ModifierValueSpec(value=1),
                )

    def test_policy_validates_every_modifier_kind_and_default_contract(self) -> None:
        modifier_kinds = {
            "flag": ResolvedValueKind.BOOL,
            "type": ResolvedValueKind.SCALAR_TYPE,
            "rounding": ResolvedValueKind.ROUNDING_MODE,
            "comparison": ResolvedValueKind.COMPARISON_OPERATOR,
            "boolean_op": ResolvedValueKind.BOOLEAN_OPERATOR,
            "cache": ResolvedValueKind.CACHE_OPERATOR,
            "eviction_priority": ResolvedValueKind.EVICTION_PRIORITY,
            "prefetch_size": ResolvedValueKind.PREFETCH_SIZE,
            "semantics": ResolvedValueKind.MEMORY_CONSISTENCY,
            "scope": ResolvedValueKind.MEMORY_SCOPE,
            "vector": ResolvedValueKind.VECTOR_ARITY,
            "state_space": ResolvedValueKind.MEMORY_STATE_SPACE,
            "phase_type": ResolvedValueKind.MBARRIER_PHASE_TYPE,
            "mbarrier_layout": ResolvedValueKind.MBARRIER_LAYOUT,
            "proxy": ResolvedValueKind.ASYNC_PROXY_KIND,
            "proxy_pair": ResolvedValueKind.PROXY_KIND_PAIR,
        }
        self.assertEqual(
            frozenset(modifier_kinds.values()),
            POLICY_MODIFIER_VALUE_KINDS,
        )

        for source_kind, value_kind in modifier_kinds.items():
            self.assertIs(modifier_value_kind(ModifierKind(source_kind)), value_kind)
            policy = resolved_modifier_value_policy(value_kind)
            with self.assertRaises(ValueError):
                _build_modifier_value_availability(
                    ModifierSpec(
                        name=source_kind,
                        kind=ModifierKind(source_kind),
                        presence=ModifierPresence.REQUIRED,
                    ),
                    ModifierValueSpec(value=1),
                )
            value = (
                True
                if policy.python_type is bool
                else next(
                    value
                    for value in cpp_domain(
                        resolved_modifier_value_traits(value_kind).cpp_domain,
                        backend=self.backend,
                    ).values
                    if is_semantic_value(
                        semantic_domain_for_modifier(ModifierKind(source_kind)),
                        value,
                    )
                )
            )
            modifier = ModifierSpec(
                name=source_kind,
                kind=ModifierKind(source_kind),
                presence=ModifierPresence.REQUIRED,
            )
            resolved = _build_modifier_value_availability(
                modifier,
                ModifierValueSpec(value=value),
            )
            self.assertIs(resolved.value_kind, value_kind)

            if policy.supports_default:
                default = _build_modifier_default(
                    ModifierSpec(
                        name=source_kind,
                        kind=ModifierKind(source_kind),
                        presence=ModifierPresence.OPTIONAL,
                        default=value,
                    )
                )
                self.assertIsNotNone(default)
                self.assertIs(default.value_kind, value_kind)
            else:
                with self.assertRaisesRegex(ValueError, "is unsupported"):
                    _build_modifier_default(
                        ModifierSpec(
                            name=source_kind,
                            kind=ModifierKind(source_kind),
                            presence=ModifierPresence.OPTIONAL,
                            default=value,
                        )
                    )

        with self.assertRaisesRegex(ValueError, "flag value must be boolean"):
            _build_modifier_value_availability(
                ModifierSpec(
                    name="flag",
                    kind=ModifierKind.FLAG,
                    presence=ModifierPresence.REQUIRED,
                ),
                ModifierValueSpec(value=1),
            )
        with self.assertRaisesRegex(
            ValueError,
            "optional state-space modifier 'state_space' must have a string default",
        ):
            _build_modifier_default(
                ModifierSpec(
                    name="state_space",
                    kind=ModifierKind.STATE_SPACE,
                    presence=ModifierPresence.OPTIONAL,
                    default=1,
                )
            )

    def test_checker_value_kind_spelling_covers_every_modifier_policy(self) -> None:
        from ptx_frontend.code_gen.emit.checker_descriptors import (
            _emit_modifier_value_domain_descriptor,
        )
        for value_kind in POLICY_MODIFIER_VALUE_KINDS:
            policy = resolved_modifier_value_policy(value_kind)
            value = (
                True
                if policy.python_type is bool
                else next(
                    iter(
                        cpp_domain(
                            resolved_modifier_value_traits(value_kind).cpp_domain,
                            backend=self.backend,
                        ).values
                    )
                )
            )
            emitted = _emit_modifier_value_domain_descriptor(
                ResolvedModifierValueDomain(
                    source_kind_id="test",
                    value_kind=value_kind,
                    value=value,
                ), backend=self.backend
            )
            self.assertIn(
                f".value_kind = checker::ModifierValueKind::{value_kind.value},",
                emitted,
            )

    def test_field_views_leave_unselected_members_disengaged(self) -> None:
        from ptx_frontend.code_gen.emit.operand_views import (
            emit_check_modifier_view,
        )

        instruction = SimpleNamespace(cpp_name="Sample")
        variant = SimpleNamespace(cpp_name="Variant")
        for storage in (
            ResolvedFieldStorage.STATIC_CONSTANT,
            ResolvedFieldStorage.INSTANCE,
        ):
            emitted = emit_check_modifier_view(
                instruction,
                variant,
                ResolvedField(
                    name="type",
                    value_kind=ResolvedValueKind.SCALAR_TYPE,
                    origin=ResolvedFieldOrigin.MODIFIER,
                    source_name="type",
                    storage=storage,
                    constant_value=(
                        "f32"
                        if storage is ResolvedFieldStorage.STATIC_CONSTANT
                        else None
                    ),
                ),
                self.backend,
            )
            self.assertIn(
                ".scalar_type = "
                + (
                    "Sample::Variant::type"
                    if storage is ResolvedFieldStorage.STATIC_CONSTANT
                    else "selected.type.value"
                ),
                emitted,
            )
            self.assertIn(".cache_operator = std::nullopt", emitted)
            self.assertIn(".bool_value = std::nullopt", emitted)

    def test_inconsistent_default_rejects_before_backend_default_kind_lookup(self) -> None:
        from ptx_frontend.code_gen.emit.resolved_descriptors import (
            _emit_modifier_default_descriptor,
        )

        with self.assertRaisesRegex(ValueError, "unsupported modifier default"):
            _emit_modifier_default_descriptor(
                ResolvedModifierBinding(
                    source_kind_id="comparison",
                    target_field_id="comparison",
                    default_value=ResolvedModifierDefault(
                        value_kind=ResolvedValueKind.COMPARISON_OPERATOR,
                        value="eq",
                    ),
                ),
                self.backend,
            )

    def test_modifier_descriptor_members_select_only_the_kind_member(self) -> None:
        from ptx_frontend.code_gen.resolved_value_traits import (
            modifier_value_descriptor_members,
        )

        members = modifier_value_descriptor_members(
            ResolvedValueKind.SCALAR_TYPE,
            "f32",
            backend=self.backend,
        )

        self.assertEqual(
            members[ResolvedValueKind.SCALAR_TYPE],
            "ScalarType::F32",
        )
        self.assertEqual(
            members[ResolvedValueKind.BOOL],
            "false",
        )
        self.assertEqual(
            members[ResolvedValueKind.ROUNDING_MODE],
            "RoundingMode::Invalid",
        )
        self.assertEqual(
            members[ResolvedValueKind.CACHE_OPERATOR],
            "CacheOperator::Unspecified",
        )
        self.assertEqual(
            frozenset(members),
            POLICY_MODIFIER_VALUE_KINDS,
        )
        self.assertNotIn("scalar_type", members)

    def test_bool_descriptor_member_does_not_require_a_cpp_domain(self) -> None:
        from ptx_frontend.code_gen.resolved_value_traits import (
            modifier_value_descriptor_members,
        )

        members = modifier_value_descriptor_members(
            ResolvedValueKind.BOOL,
            True,
            backend=self.backend,
        )

        self.assertEqual(
            members[ResolvedValueKind.BOOL],
            "true",
        )
        self.assertEqual(
            members[ResolvedValueKind.SCALAR_TYPE],
            "ScalarType::Invalid",
        )

if __name__ == "__main__":
    unittest.main()
