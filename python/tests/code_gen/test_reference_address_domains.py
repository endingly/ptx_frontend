"""Prove finite descriptor association without creating instruction permissions."""
from dataclasses import replace
import unittest
from ptx_frontend.code_gen.context import build_generation_context
from ptx_frontend.code_gen.cpp_backend import load_cpp_backend
from ptx_frontend.code_gen.render_models.resolved_source import build_visit, build_form_methods
from ptx_frontend.ir.resolved_ir import ResolvedValueKind
from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.resources import packaged_backend_spec

class ReferenceAddressDomainTests(unittest.TestCase):
    """Existing exact variant/layout metadata remains the sole context source."""
    @classmethod
    def setUpClass(cls):
        """Load one immutable canonical generation context."""
        cls.backend=load_cpp_backend(packaged_backend_spec())
        cls.context=build_generation_context(get_packaged_spec_database(),cls.backend)

    def test_finite_associations_and_two_unconstrained_exceptions(self):
        """All4132 address incidences reference the existing exact descriptor index."""
        count=0;exceptions=[]
        for entry in self.context.entries:
            for vi,variant in enumerate(entry.resolved.variants):
                visit=build_visit(entry,variant,self.backend,vi)
                for li,layout in enumerate(variant.operand_layouts):
                    callbacks={c.member.strip('(*)'):c for c in visit.layouts[li].callbacks if c.operand_descriptor}
                    for fi,field in enumerate(layout.fields):
                        if field.value_kind not in {ResolvedValueKind.ADDRESS,ResolvedValueKind.MOV_SOURCE,ResolvedValueKind.TENSOR_OPERAND,ResolvedValueKind.SYMBOL}:continue
                        count+=1
                        callback=next(c for c in visit.layouts[li].callbacks if c.operand_descriptor and f'.bindings[{fi}]' in c.operand_descriptor)
                        self.assertIn(f'.variants[{vi}].operand_layouts[{li}].bindings[{fi}]',callback.operand_descriptor)
                        binding=layout.bindings[fi]
                        self.assertEqual(binding.target_field_id,field.name)
                        self.assertEqual(callback.selected_space=='std::nullopt',binding.state_space_modifier_field_id is None)
                        if not binding.allowed_address_state_spaces and binding.state_space_modifier_field_id is None:
                            exceptions.append((variant.variant_id,field.name))
        self.assertEqual(count,4132)
        self.assertEqual(set(exceptions),{('mov_scalar','src'),('tcgen05_alloc_generic','dst')})

    def test_global_ordinals_and_bad_associations(self):
        """Nonzero sliced variants retain global ordinals; malformed metadata fails."""
        entry=next(e for e in self.context.entries if e.specification.opcode=='ld')
        vi=next(i for i,v in enumerate(entry.resolved.variants) if v.variant_id=='ld_explicit_vector')
        model=build_form_methods(entry,[(vi,entry.resolved.variants[vi])],self.backend)[0]
        address=next(c for c in model.visit.layouts[0].callbacks if c.operand_descriptor)
        self.assertIn(f'.variants[{vi}]',address.operand_descriptor)
        self.assertEqual(address.selected_space,'state_space.value')
        variant=entry.resolved.variants[vi];layout=variant.operand_layouts[0]
        bindings=tuple(replace(b,state_space_modifier_field_id='no_such_modifier') if b.state_space_modifier_field_id else b for b in layout.bindings)
        bad=replace(variant,operand_layouts=(replace(layout,bindings=bindings),))
        with self.assertRaisesRegex(ValueError,'unsupported address state-space association'):
            build_visit(entry,bad,self.backend,vi)

    def test_supported_emit_wrapper_keeps_nonzero_and_sharded_ordinals(self):
        """The supported fragment wrapper derives current global identity, never zero."""
        from ptx_frontend.code_gen.artifact_layout import form_shards
        from ptx_frontend.code_gen.emit.resolved_source import _emit_visit
        for opcode in ('ld','cp'):
            entry=next(e for e in self.context.entries if e.specification.opcode==opcode)
            candidates=range(1,len(entry.resolved.variants))
            if opcode=='cp':
                shards=form_shards(entry)
                self.assertGreater(len(shards),1)
                candidates=[i for shard in shards[1:] for i in shard]
            vi=next(i for i in candidates if any(c.operand_descriptor
                    for l in build_visit(entry,entry.resolved.variants[i],self.backend,i).layouts
                    for c in l.callbacks))
            text=_emit_visit(entry,entry.resolved.variants[vi],self.backend)
            self.assertIn(f'.variants[{vi}].operand_layouts[',text)
            self.assertIn('.bindings[',text)
            with self.assertRaisesRegex(ValueError,'not an exact entry variant'):
                _emit_visit(entry,replace(entry.resolved.variants[vi],variant_id='foreign'),self.backend)
