"""Finite expected metadata and same-binding actual Parameter projection controls."""
from dataclasses import replace
import unittest
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.render_models.checker_descriptors import _build_modifier_value
from ptx_frontend.code_gen.render_models.operand_views import _parameter_qualifier_projection
from ptx_frontend.code_gen.resolved_value_traits import parameter_qualifier_cpp_expr
from ptx_frontend.ir.resolved_ir import ResolvedValueKind, ResolvedFieldStorage
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.resources import packaged_backend_spec

class ParameterModifierIdentityTests(unittest.TestCase):
    """Protect the adopted finite mapping without changing normalized inputs."""
    @classmethod
    def setUpClass(cls):
        """Load canonical once-lowered generation inputs."""
        cls.backend=load_cpp_backend(packaged_backend_spec())
        cls.context=build_generation_context(get_packaged_spec_database(),cls.backend)

    def test_all_fifteen_expected_and_actual_fields(self):
        """All fixed/dynamic fields retain qualifier and association identity."""
        fixed=dynamic=0
        aliases={"param":"Default","param::entry":"Entry","param::func":"Function"}
        for entry in self.context.entries:
            for variant in entry.resolved.variants:
                fields=[f for f in variant.modifier_fields if any(d.source_kind_id==f.source_name and d.value in aliases for d in variant.modifier_value_domains)]
                for field in fields:
                    for row in (*variant.modifier_value_domains,*variant.modifier_value_availabilities):
                        if row.source_kind_id==field.source_name:
                            model=_build_modifier_value(row,self.backend)
                            self.assertEqual(model.parameter_qualifier,'ParameterAddressQualifier::'+aliases.get(row.value,'Default'))
                    expression=_parameter_qualifier_projection(variant,field,self.backend)
                    self.assertNotIn('parameter_role',expression)
                    self.assertNotIn('enclosing_function',expression)
                    if field.storage is ResolvedFieldStorage.STATIC_CONSTANT:
                        fixed+=1
                        self.assertIn('qualifier = ParameterAddressQualifier::'+aliases[field.constant_value],expression)
                    else:
                        dynamic+=1
                        self.assertIn('selected.address.value.parameter_qualifier',expression)
                        self.assertIn('qualifier = std::nullopt',expression)
                        self.assertNotIn('selected.dst',expression)
                        self.assertNotIn('selected.src',expression)
        self.assertEqual((fixed,dynamic),(11,4))

    def test_unknown_alias_and_uncovered_binding_reject(self):
        """Unknown aliases or missing required associations never become Default."""
        for bad in ('param::bogus','parameter',False):
            with self.assertRaises(ValueError):
                parameter_qualifier_cpp_expr(ResolvedValueKind.MEMORY_STATE_SPACE,bad)
        variant=next(v for e in self.context.entries for v in e.resolved.variants if v.variant_id=='ld_explicit_scalar')
        field=next(f for f in variant.modifier_fields if f.name=='state_space')
        layouts=tuple(replace(l,bindings=tuple(replace(b,state_space_modifier_field_id=None) for b in l.bindings)) for l in variant.operand_layouts)
        with self.assertRaisesRegex(ValueError,'missing required'):
            _parameter_qualifier_projection(replace(variant,operand_layouts=layouts),field,self.backend)
