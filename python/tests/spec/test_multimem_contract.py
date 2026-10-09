"""Closed PTX 9.3 source and static operand inventory for multimem forms."""

import unittest

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.model import (AsyncCompletionKind, ModifierPresence,
                                     OperandAddressBasePolicy, OperandAddressOffsetDomain, OperandKind)


class MultimemContractTests(unittest.TestCase):
    """Check the seven source families against the fixed PTX 9.3 legality tables."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the shipped canonical model rather than a copied test fixture."""

        model = get_packaged_spec_database()
        entries = [entry for entry in model.instructions if entry.opcode == "multimem"]
        assert len(entries) == 1
        cls.variants = entries[0].variants

    @staticmethod
    def _field(variant, name):
        """Return a typed modifier slot by semantic role."""

        return next((field for field in variant.modifiers if field.name == name), None)

    @classmethod
    def _family(cls, variant):
        """Classify the exact fixed suffix sequence of a canonical form."""

        name = variant.name
        if name.startswith("multimem_cp_reduce_async_bulk_"):
            return "cp.reduce.async.bulk"
        if name.startswith("multimem_cp_async_bulk_"):
            return "cp.async.bulk"
        if name.startswith("multimem_st_async_release"):
            return "st.async"
        if name == "multimem_red_async_release":
            return "red.async"
        for family in ("ld_reduce", "st", "red"):
            if name.startswith(f"multimem_{family}_"):
                return family
        raise AssertionError(name)

    def test_all_seven_families_and_completion(self) -> None:
        """Do not confuse release async initiation with bulk-group completion."""

        families = {self._family(variant) for variant in self.variants}
        self.assertEqual(families, {"ld_reduce", "st", "red", "st.async",
                                    "red.async", "cp.async.bulk", "cp.reduce.async.bulk"})
        for variant in self.variants:
            family = self._family(variant)
            expected = (AsyncCompletionKind.BULK_GROUP if family.startswith("cp.")
                        else AsyncCompletionKind.NONE)
            self.assertIs(variant.completion_kind, expected)
            self.assertEqual(len(variant.operand_layouts), 1)
            operands = variant.operand_layouts[0].operands
            addresses = [operand for operand in operands if operand.kind is OperandKind.ADDRESS]
            self.assertEqual(len(addresses), 2 if family.startswith("cp.") else 1)
            for address in addresses:
                self.assertIs(address.address_offset_domain,
                              OperandAddressOffsetDomain.SIGNED32)
            if family in {"st.async", "red.async"}:
                self.assertIs(addresses[0].address_base_policy,
                              OperandAddressBasePolicy.REGISTER)

    def test_integer_and_floating_operation_tables(self) -> None:
        """Reject the manual's contradictory max.f64 red example by its Syntax table."""

        integer_ops = {"add": {"u32", "u64", "s32"},
                       "min": {"u32", "u64", "s32", "s64"},
                       "max": {"u32", "u64", "s32", "s64"},
                       "and": {"b32", "b64"}, "or": {"b32", "b64"},
                       "xor": {"b32", "b64"}}
        for variant in self.variants:
            family = self._family(variant)
            if family not in {"ld_reduce", "st", "red"}:
                continue
            op = self._field(variant, "operation")
            type_slot = self._field(variant, "type")
            types = {value.value for value in type_slot.values}
            if "_integer_" in variant.name:
                if family == "st":
                    self.assertEqual(types, {"b32", "b64", "u32", "u64", "s32", "s64"})
                else:
                    self.assertEqual(types, integer_ops[op.token[1:]])
            elif family == "red":
                self.assertEqual(op.token, ".add")
                self.assertFalse(types & {"e5m2", "e4m3", "e5m2x2", "e4m3x2", "e5m2x4", "e4m3x4"})
            elif family == "ld_reduce" and op.token in {".min", ".max"}:
                self.assertFalse(types & {"f32", "f64"})

    def test_semantics_and_scope_omission_contract(self) -> None:
        """Keep red independent defaults separate from ld/store weak ordering."""

        for variant in self.variants:
            family = self._family(variant)
            if family not in {"ld_reduce", "st", "red"}:
                continue
            semantics = self._field(variant, "semantics")
            scope = self._field(variant, "scope")
            if family == "red":
                self.assertIs(semantics.presence, ModifierPresence.OPTIONAL)
                self.assertIs(scope.presence, ModifierPresence.OPTIONAL)
                self.assertEqual(semantics.default, "omitted")
                self.assertEqual(scope.default, "none")
            elif variant.name.endswith("_weak"):
                self.assertIs(semantics.presence, ModifierPresence.OPTIONAL)
                self.assertIsNone(scope)
            else:
                self.assertIs(semantics.presence, ModifierPresence.REQUIRED)
                self.assertIs(scope.presence, ModifierPresence.REQUIRED)

    def test_float_vector_width_and_accumulation_intersection(self) -> None:
        """Each encoded type/vector/acc combination must belong to every PTX table."""

        table = {
            None: {"f16x2", "bf16x2", "f32", "f64", "e5m2x4", "e4m3x4"},
            "v2": {"f16", "f16x2", "bf16", "bf16x2", "f32", "e5m2x2", "e5m2x4", "e4m3x2", "e4m3x4"},
            "v4": {"f16", "f16x2", "bf16", "bf16x2", "f32", "e5m2", "e5m2x2", "e5m2x4", "e4m3", "e4m3x2", "e4m3x4"},
            "v8": {"f16", "bf16", "e5m2", "e5m2x2", "e4m3", "e4m3x2"},
        }
        half = {"f16", "f16x2", "bf16", "bf16x2"}
        fp8 = {"e5m2", "e5m2x2", "e5m2x4", "e4m3", "e4m3x2", "e4m3x4"}
        for variant in self.variants:
            if "_float_" not in variant.name:
                continue
            family = self._family(variant)
            vec = self._field(variant, "vector")
            kind = vec.value if vec is not None else None
            types = {value.value for value in self._field(variant, "type").values}
            self.assertTrue(types)
            self.assertTrue(types <= table[kind])
            if family == "red":
                self.assertFalse(types & fp8)
            acc = self._field(variant, "acc_precision")
            if acc is not None:
                self.assertEqual(family, "ld_reduce")
                self.assertTrue(types <= (half if acc.token == ".acc::f32" else fp8))
            operands = variant.operand_layouts[0].operands
            register = next(operand for operand in operands if operand.name in {"dst", "src"})
            if vec:
                self.assertIs(register.kind, OperandKind.REGISTER_VECTOR)
            elif family in {"st", "red"} and types <= {"f32", "f64", "e5m2x4", "e4m3x4"}:
                self.assertIs(register.kind, OperandKind.REGISTER_OR_IMMEDIATE)
            else:
                self.assertIs(register.kind, OperandKind.REGISTER)
            alignment = variant.address_alignments[0]
            self.assertEqual(alignment.vector_modifier, "vector" if vec else None)

    def test_every_floating_cross_product_is_present_once(self) -> None:
        """Compare all supported float operations against the three ISA tables."""

        vector_table = {
            None: {"f16x2", "bf16x2", "f32", "f64", "e5m2x4", "e4m3x4"},
            "v2": {"f16", "f16x2", "bf16", "bf16x2", "f32", "e5m2x2", "e5m2x4", "e4m3x2", "e4m3x4"},
            "v4": {"f16", "f16x2", "bf16", "bf16x2", "f32", "e5m2", "e5m2x2", "e5m2x4", "e4m3", "e4m3x2", "e4m3x4"},
            "v8": {"f16", "bf16", "e5m2", "e5m2x2", "e4m3", "e4m3x2"},
        }
        half = {"f16", "f16x2", "bf16", "bf16x2"}
        fp8 = {"e5m2", "e5m2x2", "e5m2x4", "e4m3", "e4m3x2", "e4m3x4"}
        expected = set()
        for vector, types in vector_table.items():
            for family in ("ld_reduce", "st", "red"):
                for op in (("add", "min", "max") if family == "ld_reduce"
                           else ("add",) if family == "red" else (None,)):
                    supported = types - (fp8 if family == "red" else set())
                    if op in {"min", "max"}:
                        supported -= {"f32", "f64"}
                    for mode in (("weak", "ordered") if family != "red" else ("red",)):
                        expected.add((family, op, vector, None, mode,
                                      frozenset(supported)))
                        if family == "ld_reduce":
                            for acc, subset in (("f32", supported & half),
                                                ("f16", supported & fp8)):
                                if subset:
                                    expected.add((family, op, vector, acc, mode,
                                                  frozenset(subset)))
        observed = {}
        for variant in self.variants:
            if "_float_" not in variant.name:
                continue
            family = self._family(variant)
            op = self._field(variant, "operation")
            vector = self._field(variant, "vector")
            acc = self._field(variant, "acc_precision")
            mode = "red" if family == "red" else ("weak" if variant.name.endswith("_weak") else "ordered")
            key = (family, op.token[1:] if op else None,
                   vector.value if vector else None,
                   acc.token.split("::")[1] if acc else None, mode)
            types = {value.value for value in self._field(variant, "type").values}
            self.assertFalse(observed.setdefault(key, set()) & types,
                             f"duplicate type in {key}")
            observed[key].update(types)
        self.assertEqual({(*key, frozenset(types)) for key, types in observed.items()},
                         expected)

    def test_scalar_literal_source_domains(self) -> None:
        """Read sources admit typed literals only where an existing decoder exists."""

        for variant in self.variants:
            family = self._family(variant)
            if family not in {"st", "red", "st.async", "red.async"}:
                continue
            operands = variant.operand_layouts[0].operands
            source = next(operand for operand in operands if operand.name == "src")
            types = {value.value for value in self._field(variant, "type").values}
            if source.kind is OperandKind.REGISTER_VECTOR:
                continue
            if types <= {"f16x2", "bf16x2"}:
                self.assertIs(source.kind, OperandKind.REGISTER)
            else:
                self.assertIs(source.kind, OperandKind.REGISTER_OR_IMMEDIATE)
                if types.isdisjoint({"f32", "f64"}):
                    self.assertEqual(source.immediate_conversion_policy.value, "narrow")

    def test_bulk_direction_mask_and_alignment(self) -> None:
        """Bulk multimem transfers have one direction and no mbarrier operand."""

        for variant in self.variants:
            if not self._family(variant).startswith("cp."):
                continue
            fields = {field.name: field for field in variant.modifiers}
            self.assertEqual(fields["dst_space"].token, ".global")
            self.assertEqual(fields["src_space"].token, ".shared::cta")
            self.assertEqual(fields["completion"].token, ".bulk_group")
            operands = {operand.name: operand for operand in variant.operand_layouts[0].operands}
            self.assertEqual(tuple(operands)[:3], ("dst", "src", "size"))
            self.assertNotIn("mbar", operands)
            self.assertEqual(variant.address_alignments[0].alignment, 16)
            self.assertEqual(variant.immediate_multiple_of.divisor, 16)
            if "cp_mask" in fields:
                self.assertIn("byte_mask", operands)
                self.assertEqual(operands["byte_mask"].kind, OperandKind.REGISTER_OR_IMMEDIATE)
            else:
                self.assertNotIn("byte_mask", operands)
            if self._family(variant) == "cp.reduce.async.bulk":
                op = fields["operation"].token
                types = {value.value for value in fields["type"].values}
                if op == ".add" and types & {"f16", "bf16"}:
                    self.assertIn("noftz", fields)
                if op in {".inc", ".dec"}:
                    self.assertEqual(types, {"u32"})


if __name__ == "__main__":
    unittest.main()
