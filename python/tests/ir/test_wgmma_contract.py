"""Canonical WGMMA inventory and bounded generated-storage contracts."""

from collections import Counter
from dataclasses import replace
from pathlib import Path
import re
import tempfile
import unittest

from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.context import GenerationContext, build_generation_context
from ptx_frontend.code_gen.emit.resolved_model import form_shards
from ptx_frontend.code_gen.emit.resolved_source import (
    generate_resolved_form_shard_source,
    generate_resolved_opcode_source,
)
from ptx_frontend.code_gen.plan import build_generation_plan
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.ir.resolved_value_kind import ResolvedValueKind
from ptx_frontend.spec.database import (
    _validate_variant_modifier_exclusivity,
    load_codegen_database_from_files,
)
from ptx_frontend.spec.model import (
    AsyncCompletionKind,
    MatrixFamily,
    MatrixFragmentRole,
    OperandKind,
    SemanticRule,
    WgmmaProtocolAction,
)
from ptx_frontend.spec.resources import packaged_backend_spec, packaged_spec_dir


SPEC = packaged_spec_dir().joinpath(
    "asynchronous_warpgroup_matrix_multiply_accumulate.yaml"
)


class WgmmaContractTests(unittest.TestCase):
    """Check the full published form census once per test class."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the installed canonical WGMMA resource and backend mappings."""

        cls.database = load_codegen_database_from_files(
            spec_files=(SPEC,)
        )
        cls.instruction = cls.database.instructions[0]
        cls.backend = load_cpp_backend(packaged_backend_spec())
        cls.context = build_generation_context(cls.database, cls.backend)

    def test_exact_matrix_and_control_census(self) -> None:
        """Retain every legal issue topology and each distinct protocol action."""

        instruction = self.instruction
        self.assertEqual(instruction.opcode, "wgmma")
        self.assertEqual(len(instruction.variants), 2151)
        self.assertEqual(
            Counter(
                variant.matrix.family if variant.matrix else variant.wgmma_protocol_action
                for variant in instruction.variants
            ),
            {
                MatrixFamily.WGMMA: 1092,
                MatrixFamily.WGMMA_SPARSE: 1056,
                WgmmaProtocolAction.REGISTER_FENCE: 1,
                WgmmaProtocolAction.COMMIT: 1,
                WgmmaProtocolAction.WAIT: 1,
            },
        )
        for variant in instruction.variants:
            self.assertIs(variant.completion_kind, AsyncCompletionKind.WGMMA_GROUP)
            if variant.matrix:
                self.assertIs(variant.wgmma_protocol_action, WgmmaProtocolAction.ISSUE)
            else:
                self.assertIsNot(variant.wgmma_protocol_action, WgmmaProtocolAction.ISSUE)

    def test_largest_accumulators_and_wait_domain(self) -> None:
        """Preserve N=256 fragments and the unbounded nonnegative wait count."""

        variants = self.instruction.variants
        large = [
            variant for variant in variants
            if variant.matrix and variant.matrix.shape.n == 256
        ]
        self.assertTrue(large)
        self.assertIn(
            128,
            {
                fragment.register_count
                for variant in large
                for fragment in variant.matrix.fragments
                if fragment.role is MatrixFragmentRole.D
            },
        )
        self.assertIn(
            64,
            {
                fragment.register_count
                for variant in large
                for fragment in variant.matrix.fragments
                if fragment.role is MatrixFragmentRole.D
            },
        )
        wait = next(
            variant for variant in variants
            if variant.wgmma_protocol_action is WgmmaProtocolAction.WAIT
        )
        self.assertEqual(len(wait.immediate_ranges), 1)
        self.assertEqual(wait.immediate_ranges[0].minimum, 0)
        self.assertIsNone(wait.immediate_ranges[0].maximum)
        self.assertEqual(wait.operand_layouts[0].operands[0].kind, OperandKind.IMMEDIATE)

    def test_signed_scale_rule_and_direct_topology(self) -> None:
        """Floating scales stay typed in every exact owned matrix form."""

        variants = self.instruction.variants
        self.assertEqual(
            sum(variant.rule is SemanticRule.MATRIX_WGMMA_SCALE for variant in variants),
            1536,
        )
        resolved = from_instruction_spec(self.instruction)
        self.assertEqual(len(resolved.variants), 2151)
        self.assertEqual(sum(variant.matrix is not None for variant in resolved.variants),
                         2148)

    def test_topology_collision_is_rejected(self) -> None:
        """Two identical modifier/arity languages need a disjoint syntax slot."""

        shared = next(
            variant for variant in self.instruction.variants
            if variant.name == "wgmma_mma_async_dense_m64n8k8_f32_tf32_tf32_shared_plain"
        )
        register = next(
            variant for variant in self.instruction.variants
            if variant.name == "wgmma_mma_async_dense_m64n8k8_f32_tf32_tf32_register_plain"
        )
        self.assertEqual(len(shared.operand_layouts[0].operands),
                         len(register.operand_layouts[0].operands))
        # The first pair has the same suffix and arity for its A placement;
        # replacing the vector-pack A with an identifier would erase the tie-break.
        register_layout = register.operand_layouts[0]
        a_operand = register_layout.operands[1]
        colliding = replace(
            register_layout,
            operands=(register_layout.operands[0],
                      replace(a_operand, kind=OperandKind.SHARED_MATRIX_DESCRIPTOR),
                      *register_layout.operands[2:]),
        )
        mutated = replace(
            self.instruction,
            variants=(shared, replace(register, operand_layouts=(colliding,))),
        )
        with self.assertRaisesRegex(ValueError, "overlapping modifier combination"):
            _validate_variant_modifier_exclusivity(mutated)

    def test_private_shards_cover_each_logical_form_once(self) -> None:
        """Check the 63/64 boundary and final partial shard in emitted switches."""

        context = self.context
        instruction = context.entries[0]
        shards = form_shards(instruction)
        count = len(shards)
        self.assertEqual(count, 34)
        intervals = [(indices[0], indices[-1] + 1) for indices in shards]
        self.assertEqual(intervals[:2], [(0, 64), (64, 128)])
        self.assertEqual(intervals[-1], (2112, 2151))
        self.assertEqual(
            [index for first, last in intervals for index in range(first, last)],
            list(range(2151)),
        )
        with tempfile.TemporaryDirectory() as directory:
            for shard in (0, 1, 33):
                path = Path(directory) / f"check_{shard}.cpp"
                generate_resolved_form_shard_source(
                    context, category="matrix", opcode="wgmma",
                    shard_index=shard, output_path=path,
                )
                self.assertEqual(
                    path.read_text().count("::instruction_kind() const noexcept"),
                    len(shards[shard]),
                )

    def test_typed_construction_shards_preserve_exact_dispatch_and_field_order(self) -> None:
        """Every WGMMA form has one private builder case and one entry dispatch."""

        entry = self.context.entries[0]
        shards = form_shards(entry)
        names = tuple(entry.cpp_name + variant.cpp_name
                      for variant in entry.resolved.variants)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entrypoint = root / "resolved_ir_matrix_wgmma.gen.cpp"
            generate_resolved_opcode_source(
                self.context, category="matrix", opcode="wgmma",
                output_path=entrypoint,
            )
            source = entrypoint.read_text(encoding="utf-8")
            dispatch = source.split("  if (!fields) return std::unexpected(fields.error());\n", 1)[1]
            dispatched = re.findall(
                r"    case (Wgmma\w+)::kind: return generated_detail::"
                r"wgmma_construct_(\d{3})\(kind, \*fields\);", dispatch,
            )
            self.assertEqual(
                dispatched,
                [(names[index], f"{shard_index:03d}")
                 for shard_index, indices in enumerate(shards) for index in indices],
            )
            self.assertEqual(source.count("std::make_unique<Wgmma"), 0)
            self.assertEqual(source.count("resolve_fields(ast,"), 1)
            self.assertEqual(source.count("select_variant_name(ast,"), 1)
            for shard_index in range(len(shards)):
                declaration = (
                    f"wgmma_construct_{shard_index:03d}(InstructionKind kind, "
                    "ResolvedInstructionFields& fields);"
                )
                self.assertIn(declaration, source)

            constructed: list[str] = []
            for shard_index, indices in enumerate(shards):
                shard_path = root / f"resolved_ir_matrix_wgmma_methods_{shard_index:03d}.gen.cpp"
                generate_resolved_form_shard_source(
                    self.context, category="matrix", opcode="wgmma",
                    shard_index=shard_index, output_path=shard_path,
                )
                emitted = shard_path.read_text(encoding="utf-8")
                self.assertIn("ptx_resolved_ir_resolution_detail.hpp", emitted)
                builder = emitted.rsplit("namespace generated_detail {\n", 1)[1]
                self.assertEqual(builder.count("std::unique_ptr<Instruction> wgmma_construct_"), 1)
                cases = re.findall(r"    case (Wgmma\w+)::kind: \{", builder)
                self.assertEqual(cases, [names[index] for index in indices])
                self.assertEqual(builder.count("std::make_unique<Wgmma"), len(indices))
                self.assertEqual(
                    builder.count("switch (fields.operand_layout.value)"),
                    len(indices),
                )
                self.assertEqual(
                    builder.count("std::move(fields.execution_predicate)"),
                    len(indices),
                )
                constructed.extend(cases)

                for index in indices:
                    variant = entry.resolved.variants[index]
                    start = builder.index(f"    case {names[index]}::kind: {{")
                    end = builder.find("    case Wgmma", start + 1)
                    form_case = builder[start:end if end != -1 else None]
                    self.assertLess(
                        form_case.index("std::move(fields.execution_predicate)"),
                        form_case.index("switch (fields.operand_layout.value)"),
                    )
                    self.assertEqual(
                        re.findall(r"      case (\d+):", form_case),
                        [str(layout) for layout in range(len(variant.operand_layouts))],
                    )
                    for layout_index, layout in enumerate(variant.operand_layouts):
                        layout_case = form_case.split(f"      case {layout_index}:\n", 1)[1]
                        next_case = layout_case.find("      case ")
                        layout_case = layout_case[:next_case if next_case != -1 else None]
                        assignments = re.findall(
                            r'resolved_operand<[^>]+>\(fields, "([^"]+)"\);',
                            layout_case,
                        )
                        self.assertEqual(assignments, [field.name for field in layout.fields])

            self.assertEqual(constructed, list(names))
            self.assertEqual(
                [constructed[index] for index in (63, 64, 2111, 2112, 2150)],
                [names[index] for index in (63, 64, 2111, 2112, 2150)],
            )
            plan = build_generation_plan(self.context, root)
            self.assertEqual(
                tuple(path for path in plan.paths if "wgmma_construct" in path.name),
                (),
            )
            self.assertEqual(
                sum("wgmma_methods_" in path.name for path in plan.paths),
                len(shards),
            )

    def test_unsharded_wgmma_keeps_inline_construction(self) -> None:
        """Reduced WGMMA snapshots still build exact forms without helpers."""

        original = self.context.entries[0]
        for count in (1, 64):
            with self.subTest(count=count), tempfile.TemporaryDirectory() as directory:
                entry = replace(
                    original,
                    specification=replace(
                        original.specification,
                        variants=original.specification.variants[:count],
                    ),
                    resolved=replace(
                        original.resolved,
                        variants=original.resolved.variants[:count],
                    ),
                )
                context = GenerationContext(
                    backend=self.context.backend, entries=(entry,)
                )
                self.assertEqual(form_shards(entry), ())
                output = Path(directory) / "wgmma.gen.cpp"
                generate_resolved_opcode_source(
                    context, category="matrix", opcode="wgmma",
                    output_path=output,
                )
                source = output.read_text(encoding="utf-8")
                resolver = source.split(
                    "/** Resolve wgmma to one exact final form. */", 1
                )[1]
                self.assertEqual(resolver.count("std::make_unique<Wgmma"), count)
                self.assertNotIn("wgmma_construct_", source)
                self.assertEqual(source.count("resolve_fields(ast,"), 1)


if __name__ == "__main__":
    unittest.main()
