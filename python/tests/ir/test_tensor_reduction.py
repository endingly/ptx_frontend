"""Canonical tiled tensor-reduction metadata and generated typed query tests."""

from dataclasses import replace
from pathlib import Path
from tempfile import TemporaryDirectory
import unittest

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.emit.resolved_model import (
    form_shards,
    generate_resolved_base_header,
    generate_resolved_form_shard_header,
)
from ptx_frontend.ir.resolved_ir import TensorAccessMode, from_instruction_spec
from ptx_frontend.ir.tensor_reduction import (
    TENSOR_REDUCTION_ELEMENT_TYPES, TensorReductionOp,
)
from ptx_frontend.spec.database import CodegenDatabase, get_packaged_spec_database
from ptx_frontend.spec.model import ModifierPresence, SemanticRule
from ptx_frontend.spec.resources import packaged_backend_spec


# Independent PTX 9.3 operation/descriptor-element-type expectation.
EXPECTED_TYPES = {
    "add": {"u32", "s32", "u64", "f32", "f16", "bf16"},
    "min": {"u32", "s32", "u64", "s64", "f16", "bf16"},
    "max": {"u32", "s32", "u64", "s64", "f16", "bf16"},
    "inc": {"u32"}, "dec": {"u32"},
    "and": {"b32", "b64"}, "or": {"b32", "b64"},
    "xor": {"b32", "b64"},
}


class TensorReductionMetadataTests(unittest.TestCase):
    """Check lowering boundaries independently of the C++ emitted model."""

    @classmethod
    def setUpClass(cls) -> None:
        database = get_packaged_spec_database()
        cls.cp = next(spec for spec in database.instructions if spec.opcode == "cp")

    def test_all_rank_operation_identities_and_other_cp_forms(self) -> None:
        resolved = from_instruction_spec(self.cp)
        selected = [v for v in resolved.variants if v.tensor_reduction_op is not None]
        self.assertEqual(len(selected), 64)
        self.assertEqual(
            {v.variant_id for v in selected},
            {f"cp_reduce_async_bulk_tensor_{rank}d_{op}"
             for rank in range(1, 6) for op in EXPECTED_TYPES}
            | {f"cp_reduce_async_bulk_tensor_{rank}d_{op}_im2col_no_offs"
               for rank in range(3, 6) for op in EXPECTED_TYPES},
        )
        for source, variant in zip(self.cp.variants, resolved.variants, strict=True):
            if source.rule is SemanticRule.DATA_MOVEMENT_TENSOR_REDUCTION:
                self.assertEqual(
                    variant.tensor_reduction_op.value,
                    next(m.token.removeprefix(".") for m in source.modifiers
                         if m.name.startswith("reduction_")),
                )
                self.assertIs(
                    variant.tensor_access_mode,
                    TensorAccessMode.IM2COL_NO_OFFS
                    if source.name.endswith("_im2col_no_offs")
                    else TensorAccessMode.TILED,
                )
            else:
                self.assertIsNone(variant.tensor_reduction_op)

    def test_conditional_type_matrix_and_generated_query(self) -> None:
        self.assertEqual(
            {op.value: set(TENSOR_REDUCTION_ELEMENT_TYPES[op])
             for op in TensorReductionOp}, EXPECTED_TYPES,
        )
        context = build_generation_context(
            CodegenDatabase(spec_schema="ptx-instr/v1", instructions=(self.cp,)),
            load_cpp_backend(packaged_backend_spec()),
        )
        with TemporaryDirectory() as directory:
            root = Path(directory)
            base = root / "ptx_instruction_base.gen.hpp"
            generate_resolved_base_header(context, output_path=base)
            text = base.read_text(encoding="utf-8")
            forms = []
            for index in range(len(form_shards(context.entries[0]))):
                shard = root / f"cp_forms_{index:03d}.gen.hpp"
                generate_resolved_form_shard_header(
                    context,
                    category=self.cp.codegen_category,
                    opcode="cp",
                    shard_index=index,
                    output_path=shard,
                )
                forms.append(shard.read_text(encoding="utf-8"))
            form_text = "\n".join(forms)
        self.assertEqual(text.count("enum class TensorReductionOp"), 1)
        self.assertIn("default:\n      return false;", text)
        for op in TensorReductionOp:
            self.assertIn(f"case TensorReductionOp::{op.value.title()}:", text)
            self.assertIn(
                f"tensor_reduction_op = TensorReductionOp::{op.value.title()};",
                form_text,
            )
        self.assertIn("class CpReduceAsyncBulkTensor1dAdd final : public Instruction", form_text)

    def test_malformed_fixed_operation_and_topology_rejected(self) -> None:
        selected = next(v for v in self.cp.variants
                        if v.rule is SemanticRule.DATA_MOVEMENT_TENSOR_REDUCTION)
        position = next(i for i, m in enumerate(selected.modifiers)
                        if m.name.startswith("reduction_"))
        operation = selected.modifiers[position]

        def modified(**changes):
            modifiers = list(selected.modifiers)
            modifiers[position] = replace(operation, **changes)
            return replace(selected, modifiers=tuple(modifiers))

        bad = (
            modified(value=False),
            modified(presence=ModifierPresence.OPTIONAL),
            modified(presence=ModifierPresence.REQUIRED, value=None),
            modified(token=".unsupported"),
            modified(token=".min"),
            modified(name="reduction_unsupported"),
            replace(selected, modifiers=selected.modifiers[:position]
                    + selected.modifiers[position + 1:]),
            replace(selected, modifiers=selected.modifiers + (operation,)),
            replace(selected, operand_layouts=()),
        )
        for variant in bad:
            with self.subTest(variant=variant):
                with self.assertRaises(ValueError):
                    from_instruction_spec(replace(self.cp, variants=(variant,)))


if __name__ == "__main__":
    unittest.main()
