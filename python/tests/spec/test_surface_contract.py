"""Canonical surface matrix, typed lowering, and availability contracts."""

import unittest
from copy import deepcopy
from pathlib import Path

from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.spec.normalize.instruction import normalize_instruction_spec
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.code_gen.emit.resolved_model import _surface_descriptor
from ptx_frontend.spec.model import SurfaceAddressingMode, SurfaceGeometry

SPEC = Path(__file__).resolve().parents[2] / 'src/ptx_frontend/spec/resources/ptx_spec/surface.yaml'


class SurfaceContractTests(unittest.TestCase):
    """Exercise positive matrix coverage and reject contradictory canonical forms."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load one canonical source shared by the bounded contract tests."""
        cls.source = load_yaml(SPEC)
        cls.instructions = normalize_instruction_spec(cls.source)

    def test_complete_mnemonic_matrix(self) -> None:
        """Count exact written modifier combinations, including omitted cache."""
        counts = {}
        for instruction in self.instructions:
            count = 0
            for variant in instruction.variants:
                product = 1
                for modifier in variant.modifiers:
                    if modifier.values:
                        product *= len(modifier.values) + (modifier.presence.value == 'optional')
                count += product
            counts[instruction.opcode] = count
        self.assertEqual(counts, {'suld': 825, 'sust': 852, 'sured': 180, 'suq': 7})

    def test_generated_typed_descriptor(self) -> None:
        """Independent surface facts and indirect gates survive typed lowering."""
        for instruction in self.instructions:
            rendered = _surface_descriptor(from_instruction_spec(instruction).variants[0])
            self.assertIn('SurfaceInstructionDescriptor surface_contract', rendered)
            self.assertIn('.data_type = dtype.value', rendered)
            self.assertIn('.minimum_ptx_version = {3, 1}', rendered)
            self.assertNotIn('Texture', rendered)

    def reject(self, opcode: str, change) -> None:
        """Require mutation of one canonical row to fail before generation."""
        specimen = deepcopy(self.source)
        instruction = next(row for row in specimen['instructions'] if row['opcode'] == opcode)
        specimen['instructions'] = [instruction]
        instruction['variants'] = [instruction['variants'][0]]
        change(instruction['variants'][0])
        with self.assertRaises((ValueError, TypeError)):
            normalize_instruction_spec(specimen)

    def test_rejects_contradictory_geometry_and_operation(self) -> None:
        """Surface cannot acquire texture geometry or reduction on a load."""
        self.reject('suld', lambda row: row['surface'].update(geometry='cube'))
        self.reject('suld', lambda row: row['surface'].update(addressing='p'))
        self.reject('sust', lambda row: row['surface'].update(operation='add'))
        self.reject('sured', lambda row: row['surface'].update(geometry='a1d'))
        self.reject('suq', lambda row: row['surface'].update(query='num_samples'))
        self.reject('suq', lambda row: row['surface'].update(geometry='1d'))

    def test_rejects_extra_or_mismatched_modifier_slots(self) -> None:
        """Extra suffixes cannot hide behind a subset of required spellings."""
        def extra(row, name: str, token: str) -> None:
            """Append one contradictory fixed source suffix."""
            row['modifiers'].append(dict(name=name, kind='flag', presence='fixed', value=True, token=token))
        self.reject('suld', lambda row: extra(row, 'cube', '.cube'))
        self.reject('suld', lambda row: extra(row, 'other_geometry', '.2d'))
        self.reject('sust', lambda row: extra(row, 'other_boundary', '.zero'))
        self.reject('sured', lambda row: extra(row, 'other_operation', '.xor'))
        self.reject('suq', lambda row: extra(row, 'other_query', '.height'))
        self.reject('suq', lambda row: next(modifier for modifier in row['modifiers'] if modifier['name'] == 'dtype')['values'].append('u32'))
        self.reject('suld', lambda row: next(modifier for modifier in row['modifiers'] if modifier['name'] == 'boundary').update(token='.zero'))

    def test_rejects_direction_type_and_order_drift(self) -> None:
        """Every family preserves exact operand direction and modifier order."""
        self.reject('suq', lambda row: row['operands'][0].update(register_width='equal_or_wider'))
        for opcode in ('suld', 'sust', 'sured', 'suq'):
            self.reject(opcode, lambda row: row['modifiers'].reverse())
            data_index = 0 if opcode in {'suld', 'suq'} else 1
            self.reject(opcode, lambda row, index=data_index: row['operands'][index].update(type='u64'))
            wrong_role = 'src' if opcode in {'suld', 'suq'} else 'dst'
            wrong_access = 'read' if opcode in {'suld', 'suq'} else 'write'
            self.reject(opcode, lambda row, index=data_index, role=wrong_role, access=wrong_access: row['operands'][index].update(role=role, access=access))
            access_index = 1 - data_index
            self.reject(opcode, lambda row, index=access_index: row['operands'][index].update(role='dst', access='write'))
            self.reject(opcode, lambda row, index=access_index: row['operands'][index].update(type='s32'))

    def test_rejects_weakened_gates(self) -> None:
        """Indirect resources, queries, and 64-bit min/max keep exact floors."""
        self.reject('suld', lambda row: row['surface'].update(indirect_availability={'ptx': '3.0', 'sm': 20}))
        self.reject('sured', lambda row: row.update(availability={'ptx': '1.5', 'sm': 0}))
        self.reject('suld', lambda row: row['modifiers'][2]['values'][0].update(availability={'ptx': '1.5', 'sm': 0}))

    def test_vector_limit_and_formatted_3d_floor(self) -> None:
        """Generic 128-bit vectors apply while sust.p.3d retains PTX 2.0."""
        for instruction in self.instructions:
            for variant in instruction.variants:
                contract = variant.surface
                dtype = next(modifier for modifier in variant.modifiers if modifier.name == 'dtype')
                if contract.vector_arity == 4:
                    self.assertNotIn('b64', {value.value for value in dtype.values})
                if instruction.opcode == 'sust' and contract.addressing is SurfaceAddressingMode.SAMPLE and contract.geometry is SurfaceGeometry.THREE_D:
                    self.assertEqual(variant.availability, {'ptx': '2.0', 'sm': 20})
        specimen = deepcopy(self.source)
        specimen['instructions'] = [specimen['instructions'][0]]
        row = next(row for row in specimen['instructions'][0]['variants'] if row['surface']['vector_arity'] == 4)
        specimen['instructions'][0]['variants'] = [row]
        next(modifier for modifier in row['modifiers'] if modifier['name'] == 'dtype')['values'].append('b64')
        with self.assertRaisesRegex(ValueError, '128'):
            normalize_instruction_spec(specimen)


if __name__ == '__main__':
    unittest.main()
