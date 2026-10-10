"""Normative PTX 9.3 Fabric/CFT source and protocol inventory."""

import unittest
from dataclasses import replace

from ptx_frontend.spec.database import get_packaged_spec_database
from ptx_frontend.spec.normalize.instruction import _validate_fabric_variant
from ptx_frontend.spec.model import (
    AsyncCompletionKind, FabricEndpointKind, FabricOperation,
    FabricSharedAccess, ModifierPresence, OperandKind,
)


class FabricContractTests(unittest.TestCase):
    """Compare generated-source inputs with the six fixed ISA families."""

    @classmethod
    def setUpClass(cls) -> None:
        """Use the packaged canonical rows used by production generation."""

        database = get_packaged_spec_database()
        fabric = [entry for entry in database.instructions if entry.opcode == "fabric"]
        fence = [entry for entry in database.instructions if entry.opcode == "fence"]
        assert len(fabric) == len(fence) == 1
        cls.forms = fabric[0].variants
        cls.fences = fence[0].variants

    def test_six_families_and_protocol_metadata(self) -> None:
        """Every exact form carries its completion, endpoint and report contract."""

        operations = {form.fabric.operation for form in self.forms}
        self.assertEqual(operations, set(FabricOperation))
        self.assertEqual(len(self.forms), 44)  # 43 semantic rows plus one source order.
        for form in self.forms:
            contract = form.fabric
            self.assertIsNotNone(contract)
            clauses = form.availability.get("any_of", [form.availability])
            self.assertTrue(all(clause["ptx"] == "9.3" for clause in clauses))
            self.assertTrue(all(clause.get("sm", 100) >= 100
                                for clause in clauses))
            layouts = form.operand_layouts
            self.assertEqual(len(layouts), 1)
            operands = layouts[0].operands
            handles = [operand for operand in operands
                       if operand.kind is OperandKind.FABRIC_HANDLE]
            if contract.operation in {FabricOperation.SUBMIT, FabricOperation.WAIT}:
                self.assertFalse(operands)
                self.assertFalse(contract.reports_fabric)
                self.assertFalse(contract.requires_mbarrier_layout_v1)
                self.assertIs(contract.endpoint, FabricEndpointKind.NONE)
                self.assertIs(contract.shared_access, FabricSharedAccess.NONE)
            else:
                self.assertEqual(len(handles), 1)
                self.assertTrue(contract.reports_fabric)
                self.assertTrue(contract.requires_mbarrier_layout_v1)
                self.assertEqual(len(operands), 5 if any(
                    operand.name in {"bytemask", "membermask"} for operand in operands
                ) else 4)
            if contract.operation in {FabricOperation.TRY_GET,
                                      FabricOperation.TRY_PULLRED}:
                self.assertIs(contract.shared_access, FabricSharedAccess.WRITE)
                self.assertIs(form.completion_kind,
                              AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES)
            elif contract.operation in {FabricOperation.TRY_PUT,
                                        FabricOperation.TRY_RED}:
                self.assertIs(contract.shared_access, FabricSharedAccess.READ)
                self.assertIs(form.completion_kind,
                              AsyncCompletionKind.MBARRIER_COMPLETE_TX16B)
            elif contract.operation is FabricOperation.WAIT:
                self.assertIs(form.completion_kind,
                              AsyncCompletionKind.FABRIC_READ_WAIT)
            else:
                self.assertIs(form.completion_kind, AsyncCompletionKind.NONE)

    def test_counted_mask_and_endpoint_cross_products(self) -> None:
        """Counted handles and byte masks never occupy the same put form."""

        put = [form for form in self.forms
               if form.fabric.operation is FabricOperation.TRY_PUT]
        red = [form for form in self.forms
               if form.fabric.operation is FabricOperation.TRY_RED]
        self.assertEqual(len(put), 7)
        self.assertEqual(len(red), 24)
        self.assertEqual({(form.fabric.endpoint, form.fabric.counted,
                           any(op.name == "bytemask" for op in form.operand_layouts[0].operands))
                          for form in put},
                         {(endpoint, counted, masked)
                          for endpoint in (FabricEndpointKind.UNICAST,
                                           FabricEndpointKind.MULTICAST)
                          for counted, masked in ((False, False), (False, True),
                                                   (True, False))})
        for form in red:
            self.assertFalse(any(op.name == "bytemask"
                                 for op in form.operand_layouts[0].operands))
        canonical = next(form for form in put
                         if form.name == "fabric_try_put_unicast_counted")
        early = next(form for form in put
                     if form.name == "fabric_try_put_unicast_counted_early")
        self.assertEqual(canonical.fabric, early.fabric)
        self.assertEqual(canonical.operand_layouts, early.operand_layouts)
        self.assertEqual(canonical.availability, early.availability)

    def test_pullred_membermask_and_submit_omission(self) -> None:
        """The pull reduction mask is an exact immediate; fetching is optional."""

        pull = [form for form in self.forms
                if form.fabric.operation is FabricOperation.TRY_PULLRED]
        self.assertEqual(len(pull), 10)
        for form in pull:
            self.assertIs(form.fabric.endpoint, FabricEndpointKind.MULTICAST)
            mask = next(op for op in form.operand_layouts[0].operands
                        if op.name == "membermask")
            self.assertIs(mask.kind, OperandKind.IMMEDIATE)
        submit = next(form for form in self.forms
                      if form.fabric.operation is FabricOperation.SUBMIT)
        fetching = next(field for field in submit.modifiers
                        if field.name == "fetching")
        self.assertIs(fetching.presence, ModifierPresence.OPTIONAL)

    def test_reduction_type_tables_and_special_targets(self) -> None:
        """Operation/type cross-products match the fixed ISA tables exactly."""

        bit = {"b32", "b64"}
        minmax = {"u32", "s32", "u64", "s64", "f16", "bf16"}
        add = {"u32", "u64", "f16", "bf16", "f32", "f64"}
        red_table = {"and": bit, "or": bit, "xor": bit,
                     "min": minmax, "max": minmax, "add": add}
        red = [form for form in self.forms
               if form.fabric.operation is FabricOperation.TRY_RED]
        for form in red:
            operation = next(field.token[1:] for field in form.modifiers
                             if field.name == "reduction")
            types = next({value.value for value in field.values}
                         for field in form.modifiers if field.name == "type")
            self.assertEqual(types, red_table[operation])
        pull_table = {
            "and_ordinary": bit, "or_ordinary": bit, "xor_ordinary": bit,
            "min_ordinary": minmax, "max_ordinary": minmax,
            "min_fp8": {"e4m3", "e5m2"},
            "max_fp8": {"e4m3", "e5m2"},
            "add_ordinary": {"u32", "u64", "f16", "bf16", "f32"},
            "add_acc_f16": {"e4m3", "e5m2"},
            "add_acc_f32": {"f16", "bf16"},
        }
        pull = [form for form in self.forms
                if form.fabric.operation is FabricOperation.TRY_PULLRED]
        self.assertEqual({form.name.removeprefix("fabric_try_pullred_")
                          for form in pull}, set(pull_table))
        for form in pull:
            suffix = form.name.removeprefix("fabric_try_pullred_")
            types = next({value.value for value in field.values}
                         for field in form.modifiers if field.name == "type")
            self.assertEqual(types, pull_table[suffix])
            if suffix in {"min_fp8", "max_fp8", "add_acc_f16",
                          "add_acc_f32"}:
                clauses = form.availability["any_of"]
                self.assertEqual({clause.get("family", clause.get("target"))
                                  for clause in clauses},
                                 {"sm_100f", "sm_110f", "sm_120a", "sm_121a"})

    def test_six_exact_fabric_proxy_fences(self) -> None:
        """Three ordered proxy pairs each support acquire and release at sys."""

        forms = [form for form in self.fences
                 if form.name.startswith("fence_proxy_") and "fabric" in form.name]
        self.assertEqual(len(forms), 6)
        self.assertEqual({form.name for form in forms},
                         {f"fence_proxy_{pair}_{sem}"
                          for pair in ("generic_to_fabric", "fabric_to_generic",
                                       "fabric_to_fabric")
                          for sem in ("acquire", "release")})
        for form in forms:
            self.assertFalse(form.operand_layouts[0].operands)
            self.assertEqual(form.availability["ptx"], "9.3")
            self.assertEqual(form.availability["sm"], 100)

    def test_generator_rejects_conflicting_contracts(self) -> None:
        """A mistyped source row cannot emit a plausible but false public view."""

        get = next(form for form in self.forms
                   if form.fabric.operation is FabricOperation.TRY_GET)
        put = next(form for form in self.forms
                   if form.name == "fabric_try_put_unicast_ordinary")
        with self.assertRaises(ValueError):
            _validate_fabric_variant(replace(
                get, fabric=replace(get.fabric, endpoint=FabricEndpointKind.MULTICAST)))
        with self.assertRaises(ValueError):
            _validate_fabric_variant(replace(
                put, fabric=replace(put.fabric, counted=True)))
        with self.assertRaises(ValueError):
            _validate_fabric_variant(replace(
                put, fabric=replace(put.fabric, endpoint=FabricEndpointKind.NONE)))
        with self.assertRaises(ValueError):
            _validate_fabric_variant(replace(
                put, completion_kind=AsyncCompletionKind.MBARRIER_COMPLETE_TX_BYTES))


if __name__ == "__main__":
    unittest.main()
