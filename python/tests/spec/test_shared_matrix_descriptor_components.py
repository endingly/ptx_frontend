"""Universal scalar descriptor components without whole-fragment permission widening."""
from collections import Counter
import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import OperandKind, OperandSpec
from ptx_frontend.spec.synatax_shapes import (OPERAND_SYNTAX_SHAPES,
    OperandSyntaxShape, operand_syntax_shapes)
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.spec.resources import packaged_backend_spec


class SharedMatrixDescriptorComponentTests(unittest.TestCase):
    """Keep the existing register-carried descriptor kind as the only production delta."""

    def test_scalar_descriptor_kind_admits_explicit_member_only(self) -> None:
        """Use the existing bit once, without globally widening IdentifierRef kinds."""
        shape = OperandSyntaxShape
        descriptor = OperandSpec(name="a_desc", kind=OperandKind.SHARED_MATRIX_DESCRIPTOR)
        self.assertEqual(operand_syntax_shapes(descriptor), shape.IDENTIFIER_REF | shape.VECTOR_MEMBER)
        self.assertEqual(OPERAND_SYNTAX_SHAPES[OperandKind.REGISTER], shape.IDENTIFIER_REF)
        self.assertEqual(OPERAND_SYNTAX_SHAPES[OperandKind.DESCRIPTOR], shape.IDENTIFIER_REF)
        self.assertFalse(operand_syntax_shapes(descriptor) & (shape.VECTOR_PACK | shape.ADDRESS | shape.IMMEDIATE))

    def test_all_existing_descriptor_fields_share_the_same_rule(self) -> None:
        """Join the fixed finite role inventory; this is not a runtime-program quota."""
        database = get_packaged_spec_database()
        counts = Counter()
        for instruction in database.instructions:
            for variant in instruction.variants:
                for layout in variant.operand_layouts:
                    for operand in layout.operands:
                        if operand.kind is OperandKind.SHARED_MATRIX_DESCRIPTOR:
                            counts[operand.name] += 1
                            self.assertEqual(operand_syntax_shapes(operand),
                                OperandSyntaxShape.IDENTIFIER_REF | OperandSyntaxShape.VECTOR_MEMBER)
        self.assertEqual(counts, {"a_desc": 1074, "b_desc": 2148})
        self.assertEqual(sum(counts.values()), 3222)

    def test_whole_named_intersection_stays_identifier_only(self) -> None:
        """A scalar component is never a whole-named matrix-fragment selection proof."""
        shape = OperandSyntaxShape
        descriptor = OperandSpec(name="a_desc", kind=OperandKind.SHARED_MATRIX_DESCRIPTOR)
        whole = OperandSpec(name="a", kind=OperandKind.MATRIX_FRAGMENT, vector_allow_named=True)
        self.assertEqual(operand_syntax_shapes(whole), shape.IDENTIFIER_REF | shape.VECTOR_PACK)
        self.assertEqual(operand_syntax_shapes(descriptor) & operand_syntax_shapes(whole), shape.IDENTIFIER_REF)
        # The normal full database/context path must preserve its independent
        # for-all layout-pair overlap checks, not replace them with this bit identity.
        database = get_packaged_spec_database()
        build_generation_context(database, load_cpp_backend(packaged_backend_spec()))


if __name__ == "__main__":
    unittest.main()
