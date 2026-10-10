"""Full-family typed video normalization and generation contracts."""
from copy import deepcopy
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.model import VideoOperandTypeUse, VideoOperation, VideoLanes
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.resources import packaged_spec_schema, packaged_spec_dir


class VideoFamilyNormalizationTests(unittest.TestCase):
    """Closed family contracts reject metadata contradictions before emission."""

    def setUp(self) -> None:
        """Load all canonical opcode slices for independent mutation tests."""
        self.raw=load_yaml(packaged_spec_dir().joinpath('video.yaml'))

    def variant(self,raw,opcode):
        """Find an exact test source row without relying on its file order."""
        return next(row for row in raw['instructions'] if row['opcode']==opcode)['variants'][0]

    def test_inventory_schema_and_topologies(self):
        """All 23 source identities carry the approved static lane topology."""
        self.assertFalse(list(Draft202012Validator(load_yaml(packaged_spec_schema())).iter_errors(self.raw)))
        instructions=normalize_instruction_spec(self.raw)
        self.assertEqual(len(instructions),23)
        expected={'vadd','vsub','vmad','vabsdiff','vmin','vmax','vshl','vshr','vset'}
        expected|={stem+str(n) for n in (2,4) for stem in ('vadd','vsub','vavrg','vabsdiff','vmin','vmax','vset')}
        self.assertEqual({instruction.opcode for instruction in instructions},expected)
        counts={lanes:0 for lanes in VideoLanes}
        for instruction in instructions:
            counts[instruction.variants[0].video.lanes]+=1
        self.assertEqual(counts,{VideoLanes.SCALAR:9,VideoLanes.TWO:7,VideoLanes.FOUR:7})

    def test_mad_c_bit_carrier_is_explicit(self):
        """Mad C coercion does not accidentally follow independent written dtype."""
        instructions=normalize_instruction_spec(self.raw)
        mad=next(row for row in instructions if row.opcode=='vmad').variants[0]
        self.assertIs(mad.video.operation,VideoOperation.MAD)
        c=mad.operand_layouts[0].operands[3].video
        self.assertIs(c.type_use,VideoOperandTypeUse.BIT_CARRIER)
        self.assertIsNone(c.type_modifier)
        for opcode,index,key,value in [('vmad',3,'type_modifier','dtype'),
                                      ('vmad',1,'type_use','bit_carrier'),
                                      ('vadd',3,'type_use','bit_carrier')]:
            raw=deepcopy(self.raw)
            variant=self.variant(raw,opcode)
            layout=variant['operand_layouts'][0 if opcode=='vmad' else 1]
            slot=layout['operands'][index]['video']
            slot[key]=value
            if opcode=='vadd': slot.pop('type_modifier',None)
            with self.subTest(opcode=opcode,index=index,key=key):
                with self.assertRaises(ValueError): normalize_instruction_spec(raw)

    def test_simd_rejects_scalar_immediates_and_negation(self):
        """Packed selectors and source carriers cannot inherit scalar widening."""
        for opcode in ('vadd2','vset2','vadd4','vset4'):
            for key in ('allow_immediate','allow_negate'):
                raw=deepcopy(self.raw)
                self.variant(raw,opcode)['operand_layouts'][0]['operands'][1]['video'][key]=True
                with self.subTest(opcode=opcode,key=key):
                    with self.assertRaises(ValueError): normalize_instruction_spec(raw)

    def test_wrong_selector_role_and_lane_family_rejected(self):
        """Packed masks, swizzles and full C roles have disjoint policies."""
        for opcode,index,selector in [('vadd2',0,'half_swizzle'),('vadd4',1,'half_swizzle'),
                                      ('vset4',3,'byte_mask'),('vadd',1,'required_scalar')]:
            raw=deepcopy(self.raw)
            self.variant(raw,opcode)['operand_layouts'][0]['operands'][index]['video']['selector']=selector
            with self.subTest(opcode=opcode,index=index):
                with self.assertRaises(ValueError): normalize_instruction_spec(raw)

    def test_comparison_output_and_c_are_unsigned(self):
        """Set instructions use two written types and implicit unsigned d/c."""
        for instruction in normalize_instruction_spec(self.raw):
            if instruction.variants[0].video.operation is not VideoOperation.COMPARE: continue
            for layout in instruction.variants[0].operand_layouts:
                for operand in layout.operands:
                    if operand.video.position.value in ('destination','c'):
                        self.assertIs(operand.video.type_use,VideoOperandTypeUse.UNSIGNED)
                        self.assertIsNone(operand.video.type_modifier)
