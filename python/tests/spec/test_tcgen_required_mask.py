"""Pin required unscaled non-WS masks and the generated mandatory members."""
from pathlib import Path
from dataclasses import replace
import unittest
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.resolved_layout import operand_slots
from ptx_frontend.spec.normalize.tcgen_mma import (
    validate_tcgen_mma_variant, normalize_f16_source_topology,
    normalize_tf32_source_topology, normalize_i8_source_topology,
    normalize_f8f6f4_source_topology,
)

VARIANTS = ('tcgen05_mma_f16','tcgen05_mma_tf32','tcgen05_mma_i8',
            'tcgen05_mma_f8f6f4','tcgen05_mma_sp_f16','tcgen05_mma_sp_tf32',
            'tcgen05_mma_sp_f8f6f4','tcgen05_mma_sp_i8')

class TcgenRequiredMaskTests(unittest.TestCase):
    """Presence is required while source scale and descriptor facts stay separate."""

    def test_exact_survivors_generate_eight_mandatory_mask_slots(self) -> None:
        """Every retained layout has a real vector; no implicit default is emitted."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == 'tcgen05')
        derived = from_instruction_spec(spec)
        backend = load_cpp_backend(Path(__file__).resolve().parents[3] / 'instructions/ptx_cpp_backend_spec/ptx_frontend.yaml')
        total = 0
        for name in VARIANTS:
            variant = next(v for v in spec.variants if v.name == name)
            emitted = next(v for v in derived.variants if v.variant_id == name)
            scaled_kind = name.endswith(('f16','tf32'))
            expected = tuple(f'{a}_mask{suffix}' for a in ('shared','tensor')
                             for suffix in (('_no_scale','_scale') if scaled_kind else ('',)))
            self.assertEqual(tuple(l.name for l in variant.operand_layouts), expected)
            slots = operand_slots(emitted, backend)
            mask = next(s for s in slots if s.field.name == 'disable_output_lane')
            self.assertFalse(mask.optional)
            self.assertEqual(mask.layout_indices, tuple(range(len(expected))))
            for layout in variant.operand_layouts:
                names = tuple(o.name for o in layout.operands)
                self.assertLess(names.index('disable_output_lane'), names.index('enable_input_d'))
                vector = next(o for o in layout.operands if o.name == 'disable_output_lane')
                self.assertEqual(vector.vector_allowed_register_types, ('b32','u32','s32','f32'))
                self.assertFalse(vector.vector_allow_named)
            total += len(expected)
        self.assertEqual(total, 24)

    def test_mask_removal_and_topology_absence_are_invalid(self) -> None:
        """All source adapters require exact4/8 while optional scale remains legal."""
        spec = next(i for i in get_packaged_spec_database().instructions if i.opcode == 'tcgen05')
        for name in VARIANTS:
            variant = next(v for v in spec.variants if v.name == name)
            layout = variant.operand_layouts[0]
            missing = replace(layout, operands=tuple(o for o in layout.operands if o.name != 'disable_output_lane'))
            with self.assertRaises(ValueError):
                validate_tcgen_mma_variant(replace(variant, operand_layouts=(missing,)+variant.operand_layouts[1:]))
        for adapter, scale in ((normalize_f16_source_topology, True),
                               (normalize_tf32_source_topology, True),
                               (normalize_i8_source_topology, False),
                               (normalize_f8f6f4_source_topology, False)):
            for group, count in ((1,4),(2,8)):
                raw = dict(group=group,a_placement='shared',mask_count=count)
                if scale: raw['scale_source_value'] = None
                self.assertEqual(adapter(raw).mask_count, count)
                for invalid in (None,0,1,3,5,7,9):
                    with self.assertRaises(ValueError): adapter({**raw,'mask_count':invalid})

if __name__ == '__main__':
    unittest.main()
