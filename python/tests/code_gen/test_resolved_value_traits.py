import unittest

from ptx_frontend.code_gen.cpp_backend import configure_cpp_backend, CppDomain
from ptx_frontend.code_gen.resolved_value_traits import (
    RESOLVED_MODIFIER_VALUE_KINDS,
    modifier_default_cpp_expr,
    modifier_value_cpp_expr,
    resolved_modifier_value_traits,
)
from ptx_frontend.ir.resolved_ir import ResolvedValueKind
from ptx_frontend.spec.resources import packaged_backend_spec


class ResolvedValueTraitsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        configure_cpp_backend(packaged_backend_spec())

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
            traits.python_type,
            str,
        )

    def test_bool_value_does_not_require_a_cpp_domain(self) -> None:
        traits = resolved_modifier_value_traits(ResolvedValueKind.BOOL)

        self.assertIsNone(traits.cpp_domain)
        self.assertEqual(
            modifier_value_cpp_expr(
                ResolvedValueKind.BOOL,
                True,
            ),
            "true",
        )

    def test_scalar_type_uses_backend_spelling(self) -> None:
        self.assertEqual(
            modifier_value_cpp_expr(
                ResolvedValueKind.SCALAR_TYPE,
                "f32",
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
            )

        with self.assertRaisesRegex(
            ValueError,
            "unsupported modifier default",
        ):
            modifier_default_cpp_expr(
                ResolvedValueKind.VECTOR_ARITY,
                "v4",
            )


if __name__ == "__main__":
    unittest.main()
