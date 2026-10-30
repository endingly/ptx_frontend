"""Historical omitted controls preserve source identity and typed interpretation."""
from copy import deepcopy
import unittest

from jsonschema import Draft202012Validator
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import FloatingPointDefault, ModifierPresence
from ptx_frontend.spec.normalize import normalize_instruction_spec
from ptx_frontend.spec.normalize.availability import normalize_availability
from ptx_frontend.spec.resources import packaged_spec_dir, packaged_spec_schema
from ptx_frontend.spec.load_yaml import load_yaml
from ptx_frontend.ir.resolved_ir import from_instruction_spec
from ptx_frontend.code_gen.render_models.availability import build_availability
from ptx_frontend.code_gen.rendering import render_template


class HistoricalFloatDefaultTests(unittest.TestCase):
    """Check the finite omission cohorts and metadata boundaries."""

    def test_exact_omission_forms_retain_only_written_fields(self) -> None:
        """No default interpretation becomes a fabricated source modifier."""
        forms = {}
        for instruction in get_packaged_spec_database().instructions:
            resolved = from_instruction_spec(instruction)
            for variant, ir in zip(instruction.variants, resolved.variants):
                if variant.floating_point_default is None:
                    continue
                forms[variant.name] = variant.floating_point_default
                self.assertIs(ir.floating_point_default, variant.floating_point_default)
                for modifier in variant.modifiers:
                    if modifier.name in {'rounding', 'approx', 'full', 'ftz'}:
                        self.assertIs(modifier.presence, ModifierPresence.ABSENT)
                self.assertFalse({'rounding', 'approx', 'full', 'ftz'} &
                                 {field.source_name for field in ir.modifier_fields})
                gate = build_availability(variant.availability)
                for clause in gate.clauses or (gate.minimum,):
                    self.assertEqual(clause.ptx_before, (1, 4))
        approximate_ftz = FloatingPointDefault.APPROXIMATE_FLUSH_TO_ZERO
        expected = {f'{op}_implicit_f32': approximate_ftz for op in
                    ('div', 'rcp', 'sqrt', 'rsqrt', 'sin', 'cos', 'lg2', 'ex2')}
        expected.update({f'{op}_implicit_f64': FloatingPointDefault.ROUND_NEAREST_EVEN
                         for op in ('div', 'sqrt', 'mad')})
        expected['rsqrt_implicit_f64'] = FloatingPointDefault.APPROXIMATE
        self.assertEqual(forms, expected)

    def test_schema_and_normalizer_reject_unknown_or_contradictory_default(self) -> None:
        """The public interpretation has a closed domain and no explicit controls."""
        raw = load_yaml(packaged_spec_dir().joinpath('arithmetic.yaml'))
        index = next(i for i, item in enumerate(raw['instructions']) if item['opcode'] == 'div')
        raw['instructions'] = [raw['instructions'][index]]
        variant = next(v for v in raw['instructions'][0]['variants'] if v['name'] == 'div_implicit_f32')
        raw['instructions'][0]['variants'] = [variant]
        validator = Draft202012Validator(load_yaml(packaged_spec_schema()))
        validator.validate(raw)
        for value in ('unknown', '', True, 1, None):
            candidate = deepcopy(raw)
            candidate['instructions'][0]['variants'][0]['floating_point_default'] = value
            self.assertFalse(validator.is_valid(candidate))
            with self.assertRaises((ValueError, TypeError)):
                normalize_instruction_spec(candidate)
        for control in ('rounding', 'approx', 'full', 'ftz'):
            candidate = deepcopy(raw)
            modifier = next(m for m in candidate['instructions'][0]['variants'][0]['modifiers'] if m['name'] == control)
            modifier.update(presence='optional', default=False)
            with self.assertRaises(ValueError):
                normalize_instruction_spec(candidate)

    def test_exclusive_versions_propagate_and_reject_bad_intervals(self) -> None:
        """Plain and DNF gates share validation and render the exclusive endpoint."""
        for clause in ({'ptx': '1.0', 'ptx_before': '1.4'}, {'ptx_before': '1.4'}):
            for raw in (clause, {'any_of': [clause]}):
                normalized = normalize_availability(raw)
                model = build_availability(normalized)
                selected = model.minimum or model.clauses[0]
                self.assertEqual(selected.ptx_before, (1, 4))
                self.assertIn('.ptx_before = checker::PtxVersion{ 1, 4 }',
                              render_template('fragments/availability.j2', model))
        for bound in ('', '1', '1.0.0', '-1.4', '1.65536', '1.0', '0.9', None, 1.4):
            clause = {'ptx': '1.0', 'ptx_before': bound}
            for raw in (clause, {'any_of': [clause]}):
                with self.assertRaises(ValueError):
                    normalize_availability(raw)
                with self.assertRaises(ValueError):
                    build_availability(raw)
        self.assertIsNone(build_availability({'ptx': '1.0'}).minimum.ptx_before)
