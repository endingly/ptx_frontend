"""Finite caller-fact checks and selected destination normalization."""

from dataclasses import fields as dataclass_fields, replace
from pathlib import Path
import sys
import unittest

from ptx_frontend.ir.resolved_ir import (
    TensorAccessMode, TensorDestination as IrTensorDestination,
    _build_tensor_destination, from_instruction_spec,
)
from ptx_frontend.ir.tensor_reduction import TensorReductionOp
from ptx_frontend.spec.database import load_codegen_database
from ptx_frontend.spec.model import AsyncCompletionKind, ModifierPresence
from ptx_frontend.spec.tensor_map_known_facts import (
    RULE_CATALOG,
    ConvertedInteger,
    FactStatus,
    ProjectedField,
    TensorAccessContext,
    TensorDestination,
    TensorDirection,
    TensorFactRule as R,
    TensorMapKnownFacts,
    validate_tensor_access_facts,
)
from ptx_frontend.code_gen.emit.tensor_map_known_facts import (
    render_tensor_map_known_fact_query,
    render_tensor_map_known_fact_rules,
)


def field(identity: str, code: int = 0) -> ProjectedField:
    """Represent a result already validated by the accepted typed projection."""

    return ProjectedField(code=code, identity=identity, valid=True)


def context(**changes: object) -> TensorAccessContext:
    """Build one complete caller-supplied tiled read without AST identity."""

    base = TensorAccessContext(
        direction=TensorDirection.LOAD, mode=TensorAccessMode.TILED, rank=3,
        destination=TensorDestination.CTA, target_identity="sm_100a",
        coordinates=(ConvertedInteger(0), ConvertedInteger(1),
                     ConvertedInteger(2)),
        selected_variant_available=True, selected_value_available=True,
        active_atomicity_use=False,
    )
    return replace(base, **changes)


def facts(**changes: object) -> TensorMapKnownFacts:
    """Keep byte facts independent of element counts and map-object storage."""

    base = TensorMapKnownFacts(
        rank=3, element=field("u32", 2), interleave=field("none"),
        swizzle=field("none"), atomicity=field("bytes16"),
        fill=field("zero"), full_dimensions_elements=(16, 16, 16),
        tiled_box_elements=(4, 4, 4), traversal_steps_elements=(1, 1, 1),
        tensor_strides_bytes=(32, 32), inner_box_bytes=96,
        inner_tensor_bytes=96, accessed_box_bytes=16,
        map_object_address_bytes=64, data_base_address_bytes=32,
        accessed_box_address_bytes=16, shared_destination_address_bytes=128,
    )
    return replace(base, **changes)


def facts_at_rank(rank: int, **changes: object) -> TensorMapKnownFacts:
    """Keep descriptor array arities coherent when testing a selected rank."""

    base = replace(facts(), rank=rank,
                   full_dimensions_elements=(16,) * rank,
                   tiled_box_elements=(4,) * rank,
                   traversal_steps_elements=(1,) * rank)
    return replace(base, **changes)


class TensorDestinationNormalizationTests(unittest.TestCase):
    """Keep selected load destination on canonical normalized IR."""

    @classmethod
    def setUpClass(cls) -> None:
        """Load the repository's accepted Cp identities once per class."""

        database = load_codegen_database(
            spec_dir=Path(__file__).resolve().parents[3] / "instructions/ptx_spec"
        )
        cls.cp = next(item for item in database.instructions if item.opcode == "cp")
        cls.lowered = from_instruction_spec(cls.cp)

    def test_exact_load_destinations_and_nontensor_absence(self) -> None:
        """Canonical fixed qualifiers distinguish CTA, cluster and absence."""

        by_name = {item.cpp_name: item for item in self.lowered.variants}
        self.assertIs(by_name["AsyncBulkTensor1dSharedCta"].tensor_destination,
                      TensorDestination.CTA)
        self.assertIs(by_name["AsyncBulkTensor1dSharedCluster"].tensor_destination,
                      TensorDestination.CLUSTER)
        self.assertIsNone(by_name["AsyncBulkPrefetchTensor1d"].tensor_destination)
        self.assertIsNone(by_name["AsyncBulkTensor1dGlobalSharedCta"].tensor_destination)
        self.assertIsNone(by_name["AsyncWaitAll"].tensor_destination)
        self.assertIs(TensorDestination, IrTensorDestination)

    def test_malformed_load_qualifiers_reject(self) -> None:
        """Missing, nonfixed, unsupported and inconsistent loads fail lowering."""

        selected = next(item for item in self.cp.variants
                        if item.name == "cp_async_bulk_tensor_1d_shared_cta")
        mode = TensorAccessMode.TILED
        destination = next(item for item in selected.modifiers
                           if item.name == "dst_space")
        source = next(item for item in selected.modifiers
                      if item.name == "src_space")

        def modified(*, dst=None, src=None, completion=None):
            """Replace only one selected fixed qualifier for a negative case."""

            modifiers = tuple(
                dst if item.name == "dst_space" and dst is not None else
                src if item.name == "src_space" and src is not None else item
                for item in selected.modifiers
                if dst != "missing" or item.name != "dst_space"
            )
            return replace(selected, modifiers=modifiers,
                           completion_kind=completion or selected.completion_kind)

        with self.assertRaisesRegex(ValueError, "destination"):
            _build_tensor_destination(modified(dst="missing"), mode)
        with self.assertRaisesRegex(ValueError, "destination"):
            _build_tensor_destination(modified(
                dst=replace(destination, presence=ModifierPresence.OPTIONAL)), mode)
        with self.assertRaisesRegex(ValueError, "unsupported"):
            _build_tensor_destination(modified(
                dst=replace(destination, token=".shared::bogus")), mode)
        with self.assertRaisesRegex(ValueError, "global source"):
            _build_tensor_destination(modified(
                src=replace(source, token=".shared::cta")), mode)
        with self.assertRaisesRegex(ValueError, "completion"):
            _build_tensor_destination(modified(
                completion=AsyncCompletionKind.BULK_GROUP), mode)


class TensorMapKnownFactsTests(unittest.TestCase):
    """Probe each sourced predicate and its nearest missing/invalid neighbor."""

    def outcome(self, rule: R, *, selected: TensorAccessContext | None = None,
                supplied: TensorMapKnownFacts | None = None) -> FactStatus:
        """Extract only the requested per-rule result; never collapse all rules."""

        return validate_tensor_access_facts(
            selected or context(), supplied or facts(),
        ).outcome(rule).status

    def assert_matrix(self, cases: tuple[tuple[str, R, TensorAccessContext,
                                         TensorMapKnownFacts, FactStatus], ...]) -> None:
        """Compare independent PTX-rule expectations, including undecidable cases."""

        for label, rule, selected, supplied, expected in cases:
            with self.subTest(label=label, rule=rule.name):
                self.assertEqual(self.outcome(rule, selected=selected,
                                              supplied=supplied), expected)

    def test_rule_matrix_01_08(self) -> None:
        """Exercise encoded identity, tiled byte units and packed precedence."""

        c, f = context(), facts()
        packed64 = facts(element=field("b4x16_p64", 14),
                         inner_box_bytes=64, inner_tensor_bytes=64,
                         tensor_strides_bytes=(32, 32))
        packed96 = facts(element=field("b6x16_p32", 15),
                         inner_box_bytes=96, inner_tensor_bytes=96,
                         tensor_strides_bytes=(32, 32))
        coord128 = replace(c, coordinates=(ConvertedInteger(128),
                                           ConvertedInteger(1),
                                           ConvertedInteger(2)))
        self.assert_matrix((
            ("R1 ordinary valid code", R.ELEMENT_IDENTITY, c, f, FactStatus.CHECKED),
            ("R1 code15 load meaning", R.ELEMENT_IDENTITY, c,
             facts(element=field("b6x16_p32", 15)), FactStatus.CHECKED),
            ("R1 code15 wrong direction", R.ELEMENT_IDENTITY,
             replace(c, direction=TensorDirection.STORE),
             facts(element=field("b6x16_p32", 15)), FactStatus.VIOLATED),
            ("R1 code15 store meaning", R.ELEMENT_IDENTITY,
             replace(c, direction=TensorDirection.STORE),
             facts(element=field("b6p2x16", 15)), FactStatus.CHECKED),
            ("R1 code15 wrong load meaning", R.ELEMENT_IDENTITY, c,
             facts(element=field("b6p2x16", 15)), FactStatus.VIOLATED),
            ("R1 code15 interpretation absent", R.ELEMENT_IDENTITY, c,
             facts(element=ProjectedField(15, None, True)),
             FactStatus.UNRESOLVED),
            ("R1 missing projection", R.ELEMENT_IDENTITY, c,
             facts(element=None), FactStatus.UNRESOLVED),
            ("R7 code15 interpretation absent", R.PACKED_GEOMETRY,
             coord128, facts(element=ProjectedField(15, None, True)),
             FactStatus.UNRESOLVED),
            ("R7 wrong code15 interpretation cannot select geometry",
             R.PACKED_GEOMETRY, coord128,
             facts(element=field("b6p2x16", 15)), FactStatus.UNRESOLVED),
            ("R2 exact F16 reduction", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     reduction_op=TensorReductionOp.ADD),
             facts(element=field("f16", 6), reduction_interpretation="f16"),
             FactStatus.CHECKED),
            ("R2 adjacent unsupported S64 for add", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     reduction_op=TensorReductionOp.ADD),
             facts(reduction_interpretation="s64"), FactStatus.VIOLATED),
            ("R2 no-offset reduction rank one", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     mode=TensorAccessMode.IM2COL_NO_OFFS, rank=1,
                     coordinates=(ConvertedInteger(0),),
                     reduction_op=TensorReductionOp.ADD),
             facts_at_rank(1, reduction_interpretation="u32"),
             FactStatus.VIOLATED),
            ("R2 no-offset reduction rank two", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     mode=TensorAccessMode.IM2COL_NO_OFFS, rank=2,
                     coordinates=(ConvertedInteger(0), ConvertedInteger(1)),
                     reduction_op=TensorReductionOp.ADD),
             facts_at_rank(2, reduction_interpretation="u32"),
             FactStatus.VIOLATED),
            ("R2 valid but unsupported U16 for add", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     reduction_op=TensorReductionOp.ADD),
             facts(reduction_interpretation="u16"), FactStatus.VIOLATED),
            ("R2 bitwise bridge unproved", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     reduction_op=TensorReductionOp.AND),
             facts(reduction_interpretation="b32"), FactStatus.UNRESOLVED),
            ("R2 missing scalar interpretation", R.REDUCTION_TYPE,
             replace(c, direction=TensorDirection.REDUCE,
                     reduction_op=TensorReductionOp.ADD),
             facts(reduction_interpretation=None), FactStatus.UNRESOLVED),
            ("R2 load inapplicable", R.REDUCTION_TYPE, c, f,
             FactStatus.NOT_APPLICABLE),
            ("R3 sixteen byte boundary", R.BASE_BOX, c,
             facts(accessed_box_bytes=16, accessed_box_address_bytes=16),
             FactStatus.CHECKED),
            ("R3 adjacent size 17", R.BASE_BOX, c,
             facts(accessed_box_bytes=17), FactStatus.VIOLATED),
            ("R3 adjacent address plus eight", R.BASE_BOX, c,
             facts(accessed_box_address_bytes=24), FactStatus.VIOLATED),
            ("R3 zero byte box unclassified", R.BASE_BOX, c,
             facts(accessed_box_bytes=0), FactStatus.UNRESOLVED),
            ("R3 missing box bytes", R.BASE_BOX, c,
             facts(accessed_box_bytes=None), FactStatus.UNRESOLVED),
            ("R3 im2col N/A", R.BASE_BOX,
             replace(c, mode=TensorAccessMode.IM2COL), f,
             FactStatus.NOT_APPLICABLE),
            ("R4 first element step one", R.TILED_TRAVERSAL, c, f,
             FactStatus.CHECKED),
            ("R4 first element step two", R.TILED_TRAVERSAL, c,
             facts(traversal_steps_elements=(2, 1, 1)), FactStatus.VIOLATED),
            ("R4 interleaved numeric rule unsourced", R.TILED_TRAVERSAL, c,
             facts(interleave=field("bytes16", 1)), FactStatus.UNRESOLVED),
            ("R4 missing stride", R.TILED_TRAVERSAL, c,
             facts(traversal_steps_elements=None), FactStatus.UNRESOLVED),
            ("R4 im2col N/A", R.TILED_TRAVERSAL,
             replace(c, mode=TensorAccessMode.IM2COL), f,
             FactStatus.NOT_APPLICABLE),
            ("R5 packed zero-fill", R.FILL_PACKED, c, packed64,
             FactStatus.CHECKED),
            ("R5 packed OOB-NaN", R.FILL_PACKED, c,
             replace(packed64, fill=field("oob_nan", 1)),
             FactStatus.VIOLATED),
            ("R5 ordinary fill matrix incomplete", R.FILL_PACKED, c, f,
             FactStatus.UNRESOLVED),
            ("R5 missing fill", R.FILL_PACKED, c,
             replace(packed64, fill=None), FactStatus.UNRESOLVED),
            ("R6 packed load no exclusion", R.SUBBYTE_OPERATION, c,
             packed96, FactStatus.CHECKED),
            ("R6 store B4-p64 prohibited", R.SUBBYTE_OPERATION,
             replace(c, direction=TensorDirection.STORE), packed64,
             FactStatus.VIOLATED),
            ("R6 reduction packed prohibited", R.SUBBYTE_OPERATION,
             replace(c, direction=TensorDirection.REDUCE), packed96,
             FactStatus.VIOLATED),
            ("R6 exact sm120a cluster prohibited", R.SUBBYTE_OPERATION,
             replace(c, destination=TensorDestination.CLUSTER,
                     target_identity="sm_120a"), packed96,
             FactStatus.VIOLATED),
            ("R6 ordinary element N/A", R.SUBBYTE_OPERATION, c, f,
             FactStatus.NOT_APPLICABLE),
            ("R6 missing element", R.SUBBYTE_OPERATION, c,
             facts(element=None), FactStatus.UNRESOLVED),
            ("R7 sixty-four byte packed profile", R.PACKED_GEOMETRY,
             coord128, packed64, FactStatus.CHECKED),
            ("R7 ninety-six byte packed profile", R.PACKED_GEOMETRY,
             coord128, packed96, FactStatus.CHECKED),
            ("R7 adjacent byte box", R.PACKED_GEOMETRY,
             coord128, replace(packed64, inner_box_bytes=63),
             FactStatus.VIOLATED),
            ("R7 adjacent coordinate", R.PACKED_GEOMETRY,
             replace(coord128, coordinates=(ConvertedInteger(127),
                                         ConvertedInteger(1), ConvertedInteger(2))),
             packed64, FactStatus.VIOLATED),
            ("R7 zero tensor extent unclassified", R.PACKED_GEOMETRY,
             coord128, replace(packed64, inner_tensor_bytes=0),
             FactStatus.UNRESOLVED),
            ("R7 missing tensor stride", R.PACKED_GEOMETRY,
             coord128, replace(packed64, tensor_strides_bytes=None),
             FactStatus.UNRESOLVED),
            ("R7 ordinary type N/A", R.PACKED_GEOMETRY, c, f,
             FactStatus.NOT_APPLICABLE),
            ("R8 none swizzle packed", R.PACKED_SWIZZLE,
             coord128, packed64, FactStatus.CHECKED),
            ("R8 invalid 64-byte packed swizzle", R.PACKED_SWIZZLE,
             coord128, replace(packed64, swizzle=field("bytes64", 2)),
             FactStatus.VIOLATED),
            ("R8 128-byte flip prohibited", R.PACKED_SWIZZLE,
             coord128, replace(packed64, swizzle=field("bytes128", 3),
                               atomicity=field("bytes32_flip8", 2)),
             FactStatus.VIOLATED),
            ("R8 missing swizzle", R.PACKED_SWIZZLE,
             coord128, replace(packed64, swizzle=None), FactStatus.UNRESOLVED),
            ("R8 ordinary type N/A", R.PACKED_SWIZZLE, c, f,
             FactStatus.NOT_APPLICABLE),
        ))

    def test_rule_matrix_09_16(self) -> None:
        """Check layout/source applicability, pattern offsets and exact targets."""

        c, f = context(), facts()
        active = replace(c, active_atomicity_use=True)
        sw128 = facts(swizzle=field("bytes128", 3),
                      atomicity=field("bytes32", 1),
                      shared_destination_address_bytes=32)
        sw96 = facts(swizzle=field("bytes96", 4),
                     atomicity=field("bytes16", 0), inner_box_bytes=96)
        flip = facts(swizzle=field("bytes128", 3),
                     atomicity=field("bytes32_flip8", 2))
        exact = replace(c, direction=TensorDirection.STORE,
                        target_identity="sm_103a", active_atomicity_use=True,
                        coordinates=(ConvertedInteger(64), ConvertedInteger(0),
                                     ConvertedInteger(0)))
        override = facts(element=field("b6p2x16", 15),
                         inner_box_bytes=48, inner_tensor_bytes=48,
                         data_base_address_bytes=16,
                         tensor_strides_bytes=(16, 16),
                         swizzle=field("bytes64", 2),
                         atomicity=field("bytes16", 0))
        cluster = replace(c, target_identity="sm_120a",
                          destination=TensorDestination.CLUSTER)
        self.assert_matrix((
            ("R9 none interleave", R.INTERLEAVE, c, f, FactStatus.CHECKED),
            ("R9 rank-two interleave prohibited", R.INTERLEAVE,
             replace(c, rank=2, mode=TensorAccessMode.TILE_GATHER4,
                     coordinates=(ConvertedInteger(0),) * 5),
             facts_at_rank(2, interleave=field("bytes16", 1)),
             FactStatus.VIOLATED),
            ("R9 W interleave prohibited", R.INTERLEAVE,
             replace(c, mode=TensorAccessMode.IM2COL_W),
             facts(interleave=field("bytes16", 1)), FactStatus.VIOLATED),
            ("R9 channels per slice not sourced", R.INTERLEAVE, c,
             facts(interleave=field("bytes16", 1)), FactStatus.UNRESOLVED),
            ("R9 missing interleave", R.INTERLEAVE, c,
             facts(interleave=None), FactStatus.UNRESOLVED),
            ("R10 raw code with no swizzle N/A", R.SWIZZLE_ATOMICITY, c,
             facts(swizzle=field("none", 0),
                   atomicity=field("bytes64", 3)), FactStatus.NOT_APPLICABLE),
            ("R10 applied bytes64 swizzle/16 atomicity", R.SWIZZLE_ATOMICITY,
             active, facts(swizzle=field("bytes64", 2)), FactStatus.CHECKED),
            ("R10 adjacent wrong atomicity", R.SWIZZLE_ATOMICITY,
             active, facts(swizzle=field("bytes64", 2),
                           atomicity=field("bytes32", 1)), FactStatus.VIOLATED),
            ("R10 active-use provenance absent", R.SWIZZLE_ATOMICITY,
             replace(c, active_atomicity_use=None),
             facts(swizzle=field("bytes64", 2)), FactStatus.UNRESOLVED),
            ("R10 selected use absent", R.SWIZZLE_ATOMICITY, c,
             facts(swizzle=field("bytes64", 2)), FactStatus.NOT_APPLICABLE),
            ("R11 bytes32 alignment boundary", R.SWIZZLE_ALIGNMENT,
             active, sw128, FactStatus.CHECKED),
            ("R11 bytes32 alignment near miss", R.SWIZZLE_ALIGNMENT,
             active, replace(sw128, shared_destination_address_bytes=16),
             FactStatus.VIOLATED),
            ("R11 bytes64 alignment boundary", R.SWIZZLE_ALIGNMENT,
             active, replace(sw128, atomicity=field("bytes64", 3),
                             shared_destination_address_bytes=64),
             FactStatus.CHECKED),
            ("R11 bytes64 alignment near miss", R.SWIZZLE_ALIGNMENT,
             active, replace(sw128, atomicity=field("bytes64", 3)),
             FactStatus.VIOLATED),
            ("R11 shared address unknown", R.SWIZZLE_ALIGNMENT,
             active, replace(sw128, shared_destination_address_bytes=None),
             FactStatus.UNRESOLVED),
            ("R11 contradictory no-swizzle use quarantined", R.SWIZZLE_ALIGNMENT,
             active, f, FactStatus.UNRESOLVED),
            ("R11 other swizzle valid N/A", R.SWIZZLE_ALIGNMENT,
             c, f, FactStatus.NOT_APPLICABLE),
            ("R12 repeating pattern boundary", R.PATTERN_BASE_OFFSET,
             c, facts(swizzle=field("bytes32", 1),
                      shared_destination_address_bytes=256), FactStatus.CHECKED),
            ("R12 intermediate pattern offset", R.PATTERN_BASE_OFFSET,
             c, facts(swizzle=field("bytes128", 3),
                      shared_destination_address_bytes=896), FactStatus.CHECKED),
            ("R12 numeric address missing", R.PATTERN_BASE_OFFSET,
             c, facts(swizzle=field("bytes64", 2),
                      shared_destination_address_bytes=None),
             FactStatus.UNRESOLVED),
            ("R12 no swizzle N/A", R.PATTERN_BASE_OFFSET,
             c, f, FactStatus.NOT_APPLICABLE),
            ("R13 96-byte inclusive boundary", R.SWIZZLE_96,
             active, sw96, FactStatus.CHECKED),
            ("R13 box one byte over", R.SWIZZLE_96,
             active, replace(sw96, inner_box_bytes=97), FactStatus.VIOLATED),
            ("R13 W128 excluded", R.SWIZZLE_96,
             replace(active, mode=TensorAccessMode.IM2COL_W128),
             sw96, FactStatus.VIOLATED),
            ("R13 missing box byte fact", R.SWIZZLE_96,
             active, replace(sw96, inner_box_bytes=None),
             FactStatus.UNRESOLVED),
            ("R13 contradictory no-swizzle use quarantined", R.SWIZZLE_96,
             active, f, FactStatus.UNRESOLVED),
            ("R13 other swizzle valid N/A", R.SWIZZLE_96,
             c, f, FactStatus.NOT_APPLICABLE),
            ("R14 applied tile load", R.FLIP_8,
             active, flip, FactStatus.CHECKED),
            ("R14 store prohibited", R.FLIP_8,
             replace(active, direction=TensorDirection.STORE),
             flip, FactStatus.VIOLATED),
            ("R14 W profile prohibited", R.FLIP_8,
             replace(active, mode=TensorAccessMode.IM2COL_W),
             flip, FactStatus.VIOLATED),
            ("R14 selected use unknown", R.FLIP_8,
             replace(active, active_atomicity_use=None),
             flip, FactStatus.UNRESOLVED),
            ("R14 contradictory no-swizzle use quarantined", R.FLIP_8,
             active, f, FactStatus.UNRESOLVED),
            ("R14 other pair valid N/A", R.FLIP_8,
             c, f, FactStatus.NOT_APPLICABLE),
            ("R15 exact 48-byte override", R.SM103A_PACKED_STORE,
             exact, override, FactStatus.CHECKED),
            ("R15 exact 96-byte alternative", R.SM103A_PACKED_STORE,
             exact, replace(override, inner_box_bytes=96), FactStatus.CHECKED),
            ("R15 adjacent box 64 prohibited", R.SM103A_PACKED_STORE,
             exact, replace(override, inner_box_bytes=64), FactStatus.VIOLATED),
            ("R15 96-byte swizzle prohibited", R.SM103A_PACKED_STORE,
             exact, replace(override, swizzle=field("bytes96", 4)),
             FactStatus.VIOLATED),
            ("R15 64-byte swizzle needs 16 atomicity", R.SM103A_PACKED_STORE,
             exact, replace(override, atomicity=field("bytes32", 1)),
             FactStatus.VIOLATED),
            ("R15 swizzle unknown", R.SM103A_PACKED_STORE,
             exact, replace(override, swizzle=None), FactStatus.UNRESOLVED),
            ("R15 other target N/A", R.SM103A_PACKED_STORE,
             replace(exact, target_identity="sm_100a"),
             override, FactStatus.NOT_APPLICABLE),
            ("R7 generic rule displaced", R.PACKED_GEOMETRY,
             exact, override, FactStatus.NOT_APPLICABLE),
            ("R8 generic swizzle displaced", R.PACKED_SWIZZLE,
             exact, override, FactStatus.NOT_APPLICABLE),
            ("R16 exact target ordinary cluster", R.SM120A_CLUSTER,
             cluster, f, FactStatus.CHECKED),
            ("R16 packed cluster prohibited", R.SM120A_CLUSTER,
             cluster, facts(element=field("b4x16", 13)),
             FactStatus.VIOLATED),
            ("R16 applied atomicity prohibited", R.SM120A_CLUSTER,
             replace(cluster, active_atomicity_use=True),
             facts(swizzle=field("bytes64", 2)), FactStatus.VIOLATED),
            ("R16 raw code without use provenance unresolved", R.SM120A_CLUSTER,
             replace(cluster, active_atomicity_use=None),
             f, FactStatus.UNRESOLVED),
            ("R16 other target N/A", R.SM120A_CLUSTER,
             c, f, FactStatus.NOT_APPLICABLE),
        ))

    def test_pattern_derived_values(self) -> None:
        """Check the sourced repeating offset independently of status cells."""

        c = context()
        self.assertEqual(validate_tensor_access_facts(
            c, facts(swizzle=field("bytes32", 1),
                     shared_destination_address_bytes=256)
        ).outcome(R.PATTERN_BASE_OFFSET).derived_value, 0)
        self.assertEqual(validate_tensor_access_facts(
            c, facts(swizzle=field("bytes128", 3),
                     shared_destination_address_bytes=896)
        ).outcome(R.PATTERN_BASE_OFFSET).derived_value, 7)

    def test_rule_matrix_17_22(self) -> None:
        """Keep sourced violations and explicitly unknown geometry separate."""

        c, f = context(), facts()
        store = replace(c, direction=TensorDirection.STORE)
        im2col = replace(c, mode=TensorAccessMode.IM2COL,
                         info=(ConvertedInteger(65535),))
        spatial = facts(im2col_spatial_box_elements=(1,),
                        im2col_lower_edge_offsets=(-32768,),
                        im2col_upper_edge_offsets=(32767,))
        w = replace(c, mode=TensorAccessMode.IM2COL_W, rank=5,
                    coordinates=(ConvertedInteger(0),) * 5,
                    info=(ConvertedInteger(511), ConvertedInteger(31)))
        w_facts = facts(rank=5, full_dimensions_elements=(16,) * 5,
                        tiled_box_elements=(4,) * 5,
                        traversal_steps_elements=(1,) * 5,
                        im2col_spatial_box_elements=(1, 1, 1),
                        swizzle=field("bytes64", 2))
        gather = replace(c, mode=TensorAccessMode.TILE_GATHER4, rank=2,
                         coordinates=(ConvertedInteger(0),) * 5)
        gather_facts = facts(rank=2, full_dimensions_elements=(16, 16),
                             tiled_box_elements=(4, 1),
                             traversal_steps_elements=(1, 1))
        self.assert_matrix((
            ("R17 load N/A", R.STORE_BOUNDS, c, f, FactStatus.NOT_APPLICABLE),
            ("R17 known zero still lacks box transform", R.STORE_BOUNDS,
             store, f, FactStatus.UNRESOLVED),
            ("R17 converted negative S32", R.STORE_BOUNDS,
             replace(store, coordinates=(ConvertedInteger(-1, (1 << 32) - 1),
                                         ConvertedInteger(0), ConvertedInteger(0))),
             f, FactStatus.VIOLATED),
            ("R17 wrapped source converts to zero", R.STORE_BOUNDS,
             replace(store, coordinates=(ConvertedInteger(0, 1 << 32),
                                         ConvertedInteger(0), ConvertedInteger(0))),
             f, FactStatus.UNRESOLVED),
            ("R17 lower opposite edge negative", R.STORE_BOUNDS,
             replace(store, mode=TensorAccessMode.IM2COL_NO_OFFS),
             facts(im2col_lower_edge_offsets=(-1,),
                   im2col_upper_edge_offsets=(0,)), FactStatus.VIOLATED),
            ("R17 upper opposite edge positive", R.STORE_BOUNDS,
             replace(store, mode=TensorAccessMode.IM2COL_NO_OFFS),
             facts(im2col_lower_edge_offsets=(0,),
                   im2col_upper_edge_offsets=(1,)), FactStatus.VIOLATED),
            ("R17 missing runtime coordinate", R.STORE_BOUNDS,
             replace(store, coordinates=None), f, FactStatus.UNRESOLVED),
            ("R18 tiled N/A", R.IM2COL_SHAPE, c, f,
             FactStatus.NOT_APPLICABLE),
            ("R18 rank3 signed endpoints", R.IM2COL_SHAPE,
             im2col, spatial, FactStatus.UNRESOLVED),
            ("R18 adjacent signed upper overflow", R.IM2COL_SHAPE,
             im2col, replace(spatial, im2col_upper_edge_offsets=(32768,)),
             FactStatus.VIOLATED),
            ("R18 rank4 lower endpoint", R.IM2COL_SHAPE,
             replace(im2col, rank=4, coordinates=(ConvertedInteger(0),) * 4,
                     info=(ConvertedInteger(0), ConvertedInteger(0))),
             facts_at_rank(4, im2col_spatial_box_elements=(1, 1),
                   im2col_lower_edge_offsets=(-128, 0),
                   im2col_upper_edge_offsets=(127, 0)), FactStatus.UNRESOLVED),
            ("R18 rank4 adjacent signed violation", R.IM2COL_SHAPE,
             replace(im2col, rank=4, coordinates=(ConvertedInteger(0),) * 4,
                     info=(ConvertedInteger(0), ConvertedInteger(0))),
             facts_at_rank(4, im2col_spatial_box_elements=(1, 1),
                   im2col_lower_edge_offsets=(-129, 0),
                   im2col_upper_edge_offsets=(127, 0)), FactStatus.VIOLATED),
            ("R18 missing spatial box", R.IM2COL_SHAPE,
             im2col, replace(spatial, im2col_spatial_box_elements=None),
             FactStatus.UNRESOLVED),
            ("R19 tiled N/A", R.IM2COL_INFO, c, f,
             FactStatus.NOT_APPLICABLE),
            ("R19 rank3 U16 maximum", R.IM2COL_INFO,
             im2col, spatial, FactStatus.UNRESOLVED),
            ("R19 rank4 adjacent bound", R.IM2COL_INFO,
             replace(im2col, rank=4, coordinates=(ConvertedInteger(0),) * 4,
                     info=(ConvertedInteger(256), ConvertedInteger(0))),
             facts_at_rank(4), FactStatus.VIOLATED),
            ("R19 rank5 inclusive bound", R.IM2COL_INFO,
             replace(im2col, rank=5, coordinates=(ConvertedInteger(0),) * 5,
                     info=(ConvertedInteger(31),) * 3),
             facts_at_rank(5), FactStatus.UNRESOLVED),
            ("R19 omitted information", R.IM2COL_INFO,
             replace(im2col, info=None), spatial, FactStatus.UNRESOLVED),
            ("R19 runtime information unknown", R.IM2COL_INFO,
             replace(im2col, info=(ConvertedInteger(),)), spatial,
             FactStatus.UNRESOLVED),
            ("R20 ordinary im2col N/A", R.W_PROFILE,
             im2col, spatial, FactStatus.NOT_APPLICABLE),
            ("R20 W halo and offset maxima", R.W_PROFILE,
             w, w_facts, FactStatus.UNRESOLVED),
            ("R20 W adjacent halo overflow", R.W_PROFILE,
             replace(w, info=(ConvertedInteger(512), ConvertedInteger(31))),
             w_facts, FactStatus.VIOLATED),
            ("R20 W128 halo boundary is 31", R.W_PROFILE,
             replace(w, mode=TensorAccessMode.IM2COL_W128,
                     info=(ConvertedInteger(31), ConvertedInteger(31))),
             w_facts, FactStatus.UNRESOLVED),
            ("R20 W128 adjacent halo overflow", R.W_PROFILE,
             replace(w, mode=TensorAccessMode.IM2COL_W128,
                     info=(ConvertedInteger(32), ConvertedInteger(31))),
             w_facts, FactStatus.VIOLATED),
            ("R20 D/H extent two", R.W_PROFILE,
             w, replace(w_facts, im2col_spatial_box_elements=(2, 1, 1)),
             FactStatus.VIOLATED),
            ("R20 interleave excluded", R.W_PROFILE,
             w, replace(w_facts, interleave=field("bytes16", 1)),
             FactStatus.VIOLATED),
            ("R20 missing swizzle", R.W_PROFILE,
             w, replace(w_facts, swizzle=None), FactStatus.UNRESOLVED),
            ("R21 tiled N/A", R.GATHER_SCATTER, c, f,
             FactStatus.NOT_APPLICABLE),
            ("R21 five coordinate roles and box height one", R.GATHER_SCATTER,
             gather, gather_facts, FactStatus.UNRESOLVED),
            ("R21 adjacent box height two", R.GATHER_SCATTER,
             gather, replace(gather_facts, tiled_box_elements=(4, 2)),
             FactStatus.VIOLATED),
            ("R21 interleave excluded", R.GATHER_SCATTER,
             gather, replace(gather_facts, interleave=field("bytes16", 1)),
             FactStatus.VIOLATED),
            ("R21 missing row coordinates", R.GATHER_SCATTER,
             replace(gather, coordinates=None), gather_facts,
             FactStatus.UNRESOLVED),
            ("R22 selected variant and value available", R.SELECTED_AVAILABILITY,
             c, f, FactStatus.CHECKED),
            ("R22 canonical variant rejected", R.SELECTED_AVAILABILITY,
             replace(c, selected_variant_available=False), f,
             FactStatus.VIOLATED),
            ("R22 canonical value rejected", R.SELECTED_AVAILABILITY,
             replace(c, selected_value_available=False), f,
             FactStatus.VIOLATED),
            ("R22 variant availability absent", R.SELECTED_AVAILABILITY,
             replace(c, selected_variant_available=None), f,
             FactStatus.UNRESOLVED),
            ("R22 value availability absent", R.SELECTED_AVAILABILITY,
             replace(c, selected_value_available=None), f,
             FactStatus.UNRESOLVED),
        ))

    def test_catalog_is_closed_and_renderer_uses_same_rows(self) -> None:
        """Prevent a prepared generator catalog from drifting from the query."""

        self.assertEqual(set(RULE_CATALOG), set(R))
        self.assertEqual(len(RULE_CATALOG), 22)
        rendered = render_tensor_map_known_fact_rules()
        for rule in R:
            self.assertIn(f"{rule.name} = {rule.value}", rendered)
            self.assertIn(RULE_CATALOG[rule].title, rendered)
        self.assertEqual(rendered, render_tensor_map_known_fact_rules())
        self.assertNotIn("sm_110a", rendered)
        self.assertIn("tensor_known_info_bounds", rendered)
        query = render_tensor_map_known_fact_query()
        self.assertEqual(query, render_tensor_map_known_fact_query())
        self.assertIn("TensorKnownFactsReport validate_tensor_access_facts(", query)
        self.assertNotIn("inline TensorKnownFactsReport", query)
        self.assertIn("project_tensor_map_encoded_value<Value>", query)
        self.assertIn("tensor_reduction_accepts_element_type", query)
        self.assertEqual(query.count("case TensorMapFactRule::"), 22)

    def test_element_projection_and_directional_code15(self) -> None:
        """Projection validity is supplied; code 15 keeps one raw identity."""

        self.assertEqual(self.outcome(R.ELEMENT_IDENTITY), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.ELEMENT_IDENTITY, supplied=facts(element=field("b6x16_p32", 15)),
        ), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.ELEMENT_IDENTITY, selected=context(direction=TensorDirection.STORE),
            supplied=facts(element=field("b6p2x16", 15)),
        ), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.ELEMENT_IDENTITY, selected=context(direction=TensorDirection.STORE),
            supplied=facts(element=field("b6x16_p32", 15)),
        ), FactStatus.VIOLATED)
        report = validate_tensor_access_facts(
            context(), facts(element=ProjectedField(16, None, False)))
        self.assertTrue(report.diagnostics)
        self.assertEqual(report.outcome(R.ELEMENT_IDENTITY).status,
                         FactStatus.UNRESOLVED)
        self.assertEqual(report.outcome(R.SELECTED_AVAILABILITY).status,
                         FactStatus.CHECKED)
        rejected = validate_tensor_access_facts(
            context(), facts(element=ProjectedField(15, "b6x16_p32", False)))
        self.assertTrue(rejected.diagnostics)
        self.assertEqual(rejected.outcome(R.ELEMENT_IDENTITY).status,
                         FactStatus.UNRESOLVED)
        self.assertEqual(rejected.outcome(R.SELECTED_AVAILABILITY).status,
                         FactStatus.CHECKED)

    def test_reduction_reuses_accepted_operation_matrix(self) -> None:
        """Exact named types check; bit, FTZ and TF32 bridges stay unresolved."""

        selected = context(direction=TensorDirection.REDUCE,
                           reduction_op=TensorReductionOp.ADD)
        self.assertEqual(self.outcome(
            R.REDUCTION_TYPE, selected=selected,
            supplied=facts(reduction_interpretation="u32")), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.REDUCTION_TYPE, selected=selected,
            supplied=facts(reduction_interpretation="s64")), FactStatus.VIOLATED)
        bitwise = replace(selected, reduction_op=TensorReductionOp.AND)
        self.assertEqual(self.outcome(
            R.REDUCTION_TYPE, selected=bitwise,
            supplied=facts(reduction_interpretation="b32")), FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(
            R.REDUCTION_TYPE, selected=selected,
            supplied=facts(reduction_interpretation="f32",
                           element=field("f32.ftz", 8))), FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(
            R.REDUCTION_TYPE, selected=selected,
            supplied=facts(reduction_interpretation=None)), FactStatus.UNRESOLVED)

    def test_base_box_and_traversal_do_not_infer_bytes(self) -> None:
        """Byte box and element-step predicates use distinct supplied units."""

        self.assertEqual(self.outcome(R.BASE_BOX), FactStatus.CHECKED)
        self.assertEqual(self.outcome(R.BASE_BOX,
                                     supplied=facts(accessed_box_bytes=17)),
                         FactStatus.VIOLATED)
        self.assertEqual(self.outcome(R.BASE_BOX,
                                     supplied=facts(accessed_box_bytes=None)),
                         FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(R.TILED_TRAVERSAL), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.TILED_TRAVERSAL,
            supplied=facts(traversal_steps_elements=(2, 1, 1))),
            FactStatus.VIOLATED)
        self.assertEqual(self.outcome(
            R.TILED_TRAVERSAL,
            supplied=facts(traversal_steps_elements=None)),
            FactStatus.UNRESOLVED)

    def test_packed_fill_and_direction_restrictions(self) -> None:
        """Valid encoded identities can still be forbidden by selected use."""

        self.assertEqual(self.outcome(
            R.FILL_PACKED, supplied=facts(element=field("b4x16_p64", 14))),
            FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.FILL_PACKED,
            supplied=facts(element=field("b4x16_p64", 14),
                           fill=field("oob_nan", 1))), FactStatus.VIOLATED)
        self.assertEqual(self.outcome(
            R.SUBBYTE_OPERATION, selected=context(direction=TensorDirection.STORE),
            supplied=facts(element=field("b4x16_p64", 14))),
            FactStatus.VIOLATED)
        self.assertEqual(self.outcome(
            R.SUBBYTE_OPERATION, selected=context(direction=TensorDirection.REDUCE),
            supplied=facts(element=field("b4x16", 13))),
            FactStatus.VIOLATED)
        self.assertEqual(self.outcome(
            R.SUBBYTE_OPERATION, supplied=facts(element=None)),
            FactStatus.UNRESOLVED)

    def test_packed_geometry_swizzle_and_exact_override(self) -> None:
        """Exact 103a B6p2 store replaces rather than conjoins generic rules."""

        packed = facts(element=field("b6p2x16", 15),
                       swizzle=field("bytes128", 3),
                       inner_box_bytes=96, inner_tensor_bytes=96)
        selected = context(direction=TensorDirection.STORE,
                           coordinates=(ConvertedInteger(128), ConvertedInteger(1),
                                        ConvertedInteger(2)))
        self.assertEqual(self.outcome(R.PACKED_GEOMETRY,
                                      selected=selected, supplied=packed),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(R.PACKED_GEOMETRY,
                                      selected=selected,
                                      supplied=replace(packed, inner_box_bytes=95)),
                         FactStatus.VIOLATED)
        self.assertEqual(self.outcome(R.PACKED_SWIZZLE,
                                      selected=selected, supplied=packed),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.PACKED_SWIZZLE, selected=selected,
            supplied=replace(packed, atomicity=field("bytes32_flip8", 2))),
            FactStatus.VIOLATED)
        override = replace(selected, target_identity="sm_103a",
                           coordinates=(ConvertedInteger(64), ConvertedInteger(1),
                                        ConvertedInteger(2)),
                           active_atomicity_use=True)
        overridden = replace(packed, inner_box_bytes=48,
                             inner_tensor_bytes=48,
                             data_base_address_bytes=16,
                             tensor_strides_bytes=(16, 16),
                             swizzle=field("bytes64", 2))
        self.assertEqual(self.outcome(R.PACKED_GEOMETRY,
                                      selected=override, supplied=overridden),
                         FactStatus.NOT_APPLICABLE)
        self.assertEqual(self.outcome(R.PACKED_SWIZZLE,
                                      selected=override, supplied=overridden),
                         FactStatus.NOT_APPLICABLE)
        self.assertEqual(self.outcome(R.SM103A_PACKED_STORE,
                                      selected=override, supplied=overridden),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.SM103A_PACKED_STORE, selected=override,
            supplied=replace(overridden, inner_box_bytes=64)),
            FactStatus.VIOLATED)

    def test_interleave_atomicity_alignment_and_pattern(self) -> None:
        """No-swizzle atomicity is N/A; 128B alignment uses shared dst only."""

        self.assertEqual(self.outcome(R.INTERLEAVE), FactStatus.CHECKED)
        self.assertEqual(self.outcome(R.INTERLEAVE,
                                     selected=context(rank=2,
                                                      mode=TensorAccessMode.TILE_GATHER4,
                                                      coordinates=None),
                                     supplied=facts_at_rank(
                                         2, interleave=field("bytes16", 1))),
                         FactStatus.VIOLATED)
        self.assertEqual(self.outcome(R.SWIZZLE_ATOMICITY),
                         FactStatus.NOT_APPLICABLE)
        selected = context(active_atomicity_use=True)
        swizzled = facts(swizzle=field("bytes128", 3),
                         atomicity=field("bytes32", 1),
                         shared_destination_address_bytes=32)
        self.assertEqual(self.outcome(R.SWIZZLE_ATOMICITY,
                                      selected=selected, supplied=swizzled),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(R.SWIZZLE_ALIGNMENT,
                                      selected=selected, supplied=swizzled),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.SWIZZLE_ALIGNMENT, selected=selected,
            supplied=replace(swizzled, shared_destination_address_bytes=16)),
            FactStatus.VIOLATED)
        report = validate_tensor_access_facts(
            selected, replace(swizzled, swizzle=field("bytes32", 1),
                              shared_destination_address_bytes=128))
        self.assertEqual(report.outcome(R.PATTERN_BASE_OFFSET).derived_value, 1)
        self.assertEqual(self.outcome(R.SWIZZLE_ATOMICITY,
                                      selected=selected,
                                      supplied=replace(swizzled,
                                                       swizzle=field("bytes64", 2))),
                         FactStatus.VIOLATED)
        self.assertEqual(self.outcome(R.SWIZZLE_ATOMICITY,
                                      selected=context(active_atomicity_use=None),
                                      supplied=swizzled), FactStatus.UNRESOLVED)

    def test_96_byte_swizzle_and_flip(self) -> None:
        """96B and 8B-flip predicates retain independent mode restrictions."""

        selected = context(active_atomicity_use=True)
        swizzled = facts(swizzle=field("bytes96", 4), inner_box_bytes=96)
        self.assertEqual(self.outcome(R.SWIZZLE_96,
                                      selected=selected, supplied=swizzled),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(R.SWIZZLE_96,
                                      selected=selected,
                                      supplied=replace(swizzled, inner_box_bytes=97)),
                         FactStatus.VIOLATED)
        flipped = facts(swizzle=field("bytes128", 3),
                        atomicity=field("bytes32_flip8", 2))
        self.assertEqual(self.outcome(R.FLIP_8,
                                      selected=selected, supplied=flipped),
                         FactStatus.CHECKED)
        self.assertEqual(self.outcome(R.FLIP_8,
                                      selected=replace(selected,
                                                       direction=TensorDirection.STORE),
                                      supplied=flipped), FactStatus.VIOLATED)
        self.assertEqual(self.outcome(R.FLIP_8,
                                      selected=context(active_atomicity_use=None),
                                      supplied=flipped), FactStatus.UNRESOLVED)

    def test_exact_sm120_and_store_opposite_edge_signs(self) -> None:
        """Raw atomicity code alone does not prove application on sm120a."""

        cluster = context(target_identity="sm_120a",
                          destination=TensorDestination.CLUSTER)
        self.assertEqual(self.outcome(R.SM120A_CLUSTER,
                                      selected=cluster), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.SM120A_CLUSTER, selected=cluster,
            supplied=facts(element=field("b4x16", 13))),
            FactStatus.VIOLATED)
        self.assertEqual(self.outcome(
            R.SM120A_CLUSTER,
            selected=replace(cluster, active_atomicity_use=None)),
            FactStatus.UNRESOLVED)
        store = context(direction=TensorDirection.STORE,
                        coordinates=(ConvertedInteger(-1), ConvertedInteger(0),
                                     ConvertedInteger(0)))
        self.assertEqual(self.outcome(R.STORE_BOUNDS,
                                      selected=store), FactStatus.VIOLATED)
        wrapped = replace(store, coordinates=(ConvertedInteger(0, 1 << 32),
                                              ConvertedInteger(0),
                                              ConvertedInteger(0)))
        self.assertEqual(self.outcome(R.STORE_BOUNDS,
                                      selected=wrapped), FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(
            R.STORE_BOUNDS,
            selected=replace(wrapped, mode=TensorAccessMode.IM2COL_NO_OFFS),
            supplied=facts(im2col_lower_edge_offsets=(-1,),
                           im2col_upper_edge_offsets=(0,))),
            FactStatus.VIOLATED)

    def test_im2col_shape_info_w_and_four_row(self) -> None:
        """Converted U16 boundaries and corner arities preserve missing facts."""

        im2col = context(mode=TensorAccessMode.IM2COL,
                         info=(ConvertedInteger(65535),))
        spatial = facts(im2col_spatial_box_elements=(32,),
                        im2col_lower_edge_offsets=(-32768,),
                        im2col_upper_edge_offsets=(32767,))
        self.assertEqual(self.outcome(R.IM2COL_SHAPE,
                                      selected=im2col, supplied=spatial),
                         FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(
            R.IM2COL_SHAPE, selected=im2col,
            supplied=replace(spatial, im2col_upper_edge_offsets=(32768,))),
            FactStatus.VIOLATED)
        self.assertEqual(self.outcome(R.IM2COL_INFO,
                                      selected=im2col, supplied=spatial),
                         FactStatus.UNRESOLVED)
        rank4 = replace(im2col, rank=4,
                        coordinates=(ConvertedInteger(0),) * 4,
                        info=(ConvertedInteger(256), ConvertedInteger(0)))
        self.assertEqual(self.outcome(R.IM2COL_INFO,
                                      selected=rank4,
                                      supplied=facts_at_rank(4)),
                         FactStatus.VIOLATED)
        w128 = replace(im2col, mode=TensorAccessMode.IM2COL_W128,
                       rank=5, coordinates=(ConvertedInteger(0),) * 5,
                       info=(ConvertedInteger(31), ConvertedInteger(31)))
        w_facts = facts(rank=5, full_dimensions_elements=(16,) * 5,
                        tiled_box_elements=(4,) * 5,
                        traversal_steps_elements=(1,) * 5,
                        im2col_spatial_box_elements=(1, 1, 32),
                        swizzle=field("bytes64", 2))
        self.assertEqual(self.outcome(R.W_PROFILE,
                                      selected=w128, supplied=w_facts),
                         FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(R.W_PROFILE,
                                      selected=replace(w128,
                                                       info=(ConvertedInteger(32),
                                                             ConvertedInteger(31))),
                                      supplied=w_facts), FactStatus.VIOLATED)
        gather = context(mode=TensorAccessMode.TILE_GATHER4, rank=2,
                         coordinates=(ConvertedInteger(0),) * 5)
        gather_facts = facts(rank=2, full_dimensions_elements=(16, 16),
                             tiled_box_elements=(4, 1),
                             traversal_steps_elements=(1, 1))
        self.assertEqual(self.outcome(R.GATHER_SCATTER,
                                      selected=gather, supplied=gather_facts),
                         FactStatus.UNRESOLVED)
        self.assertEqual(self.outcome(
            R.GATHER_SCATTER, selected=gather,
            supplied=replace(gather_facts, tiled_box_elements=(4, 2))),
            FactStatus.VIOLATED)

    def test_selected_availability_and_malformed_claims(self) -> None:
        """Canonical query outputs are consumed without a second target DNF."""

        self.assertEqual(self.outcome(R.SELECTED_AVAILABILITY), FactStatus.CHECKED)
        self.assertEqual(self.outcome(
            R.SELECTED_AVAILABILITY,
            selected=context(selected_variant_available=False)),
            FactStatus.VIOLATED)
        self.assertEqual(self.outcome(
            R.SELECTED_AVAILABILITY,
            selected=context(selected_variant_available=None)),
            FactStatus.UNRESOLVED)
        report = validate_tensor_access_facts(
            context(coordinates=(ConvertedInteger(1, 1 << 32),) * 3), facts())
        self.assertTrue(any(item.field == "coordinates"
                            for item in report.diagnostics))
        report = validate_tensor_access_facts(
            context(mode=object()), facts())  # type: ignore[arg-type]
        self.assertTrue(any(item.field == "mode" for item in report.diagnostics))
        report = validate_tensor_access_facts(
            context(), facts(rank=0, full_dimensions_elements=(1,)))
        self.assertTrue(report.diagnostics)

    def _malformed_cases(self) -> tuple[tuple[str, TensorAccessContext,
                                               TensorMapKnownFacts, str, R], ...]:
        """Keep independently expected malformed inputs for both languages."""

        return (
            ("invalid direction", context(direction=object()), facts(),
             "direction", R.ELEMENT_IDENTITY),  # type: ignore[arg-type]
            ("unhashable mode", context(mode=[]), facts(),
             "mode", R.IM2COL_SHAPE),  # type: ignore[arg-type]
            ("invalid selected rank", context(rank=0), facts(),
             "rank", R.IM2COL_SHAPE),
            ("different known rank", context(), facts(rank=2),
             "facts.rank", R.GATHER_SCATTER),
            ("isolated invalid descriptor rank", context(),
             TensorMapKnownFacts(rank=0, element=field("u32", 2)),
             "facts.rank", R.ELEMENT_IDENTITY),
            ("isolated mismatched descriptor rank", context(),
             TensorMapKnownFacts(rank=2, element=field("u32", 2)),
             "facts.rank", R.ELEMENT_IDENTITY),
            ("invalid reduction scalar enum",
             context(direction=TensorDirection.REDUCE,
                     reduction_op=TensorReductionOp.ADD),
             facts(reduction_interpretation="invalid_scalar"),
             "reduction_interpretation", R.REDUCTION_TYPE),
            ("invalid projected field", context(),
             facts(swizzle=ProjectedField(1, "bytes32", False)),
             "swizzle", R.SWIZZLE_ATOMICITY),
            ("invalid projection code", context(),
             facts(element=ProjectedField(True, "u32", True)),
             "element", R.ELEMENT_IDENTITY),
            ("invalid code15 interpretation", context(),
             facts(element=ProjectedField(15, "u32", True)),
             "element", R.ELEMENT_IDENTITY),
            ("invalid traversal arity", context(),
             facts(traversal_steps_elements=(1,)),
             "traversal_steps_elements", R.TILED_TRAVERSAL),
            ("invalid spatial arity", context(mode=TensorAccessMode.IM2COL),
             facts(im2col_spatial_box_elements=(1, 1)),
             "im2col_spatial_box_elements", R.IM2COL_SHAPE),
            ("invalid coordinate container", context(coordinates=[]), facts(),
             "coordinates", R.PACKED_GEOMETRY),  # type: ignore[arg-type]
            ("invalid coordinate source bits", context(coordinates=(
                ConvertedInteger(0, "bad"), ConvertedInteger(0),
                ConvertedInteger(0))), facts(),
             "coordinates", R.PACKED_GEOMETRY),  # type: ignore[arg-type]
            ("invalid info container", context(mode=TensorAccessMode.IM2COL,
                                                info=[]), facts(),
             "info", R.IM2COL_INFO),  # type: ignore[arg-type]
            ("invalid info use value", context(mode=TensorAccessMode.IM2COL,
                                                info=(ConvertedInteger(True),)),
             facts(), "info", R.IM2COL_INFO),
            ("invalid availability claim",
             context(selected_value_available="yes"), facts(),
             "selected_value_available", R.SELECTED_AVAILABILITY),  # type: ignore[arg-type]
            ("invalid atomicity use",
             context(active_atomicity_use="bad"),
             facts(swizzle=field("bytes96", 4), inner_box_bytes=96),
             "active_atomicity_use", R.SWIZZLE_96),  # type: ignore[arg-type]
            ("contradictory no-swizzle use",
             context(active_atomicity_use=True), facts(),
             "active_atomicity_use", R.SWIZZLE_ATOMICITY),
        )

    def test_malformed_context_and_facts_are_diagnosed_safely(self) -> None:
        """Reject invalid metadata before any dependent rule indexes its values."""

        for label, selected, supplied, field_name, rule in self._malformed_cases():
            with self.subTest(label=label):
                report = validate_tensor_access_facts(selected, supplied)
                self.assertIn(field_name,
                              {diagnostic.field for diagnostic in report.diagnostics})
                self.assertEqual(report.outcome(rule).status,
                                 FactStatus.UNRESOLVED)

    def test_isolated_descriptor_rank_damage_is_global(self) -> None:
        """A rank error alone quarantines every rule, including element identity."""

        for known_rank in (0, 2):
            with self.subTest(known_rank=known_rank):
                report = validate_tensor_access_facts(
                    context(), TensorMapKnownFacts(
                        rank=known_rank, element=field("u32", 2)))
                self.assertEqual({d.field for d in report.diagnostics},
                                 {"facts.rank"})
                self.assertEqual(len(report.outcomes), 22)
                self.assertTrue(all(item.status is FactStatus.UNRESOLVED
                                    for item in report.outcomes))

    def test_malformed_atomicity_use_quarantines_each_dependent_rule(self) -> None:
        """The sole catalog must track every rule reading active use."""

        supplied = facts(swizzle=field("bytes96", 4), inner_box_bytes=96)
        report = validate_tensor_access_facts(
            context(active_atomicity_use="bad"), supplied)  # type: ignore[arg-type]
        self.assertEqual({d.field for d in report.diagnostics},
                         {"active_atomicity_use"})
        dependent = (R.SWIZZLE_ATOMICITY, R.SWIZZLE_ALIGNMENT,
                     R.SWIZZLE_96, R.FLIP_8, R.SM103A_PACKED_STORE,
                     R.SM120A_CLUSTER, R.W_PROFILE)
        for rule in dependent:
            self.assertIn("active_atomicity_use", RULE_CATALOG[rule].inputs)
            self.assertIs(report.outcome(rule).status, FactStatus.UNRESOLVED)
        self.assertIs(report.outcome(R.SELECTED_AVAILABILITY).status,
                      FactStatus.CHECKED)


def _matrix_cases() -> tuple[tuple[str, R, TensorAccessContext,
                                   TensorMapKnownFacts, FactStatus], ...]:
    """Collect only literal matrix inputs and expectations, never query results."""

    collector = TensorMapKnownFactsTests()
    rows: list[tuple[str, R, TensorAccessContext,
                     TensorMapKnownFacts, FactStatus]] = []
    collector.assert_matrix = lambda cases: rows.extend(cases)  # type: ignore[method-assign]
    collector.test_rule_matrix_01_08()
    collector.test_rule_matrix_09_16()
    collector.test_rule_matrix_17_22()
    if len(rows) != 134 or {rule for _, rule, _, _, _ in rows} != set(R):
        raise ValueError("literal parity matrix must cover 134 cells and 22 rules")
    return tuple(rows)


def _cpp_pascal(value: str) -> str:
    """Convert an accepted enum spelling into its generated C++ member."""

    return "".join(part.capitalize() for part in value.replace(".", "_").split("_"))


def _cpp_unsigned(value: int) -> str:
    """Render one literal uint64 source value without signed narrowing."""

    return f"UINT64_C({value})"


def _cpp_projected(name: str, claim: ProjectedField) -> list[str]:
    """Render a bounded claim through the existing C++ projected carrier."""

    types = {
        "element": "TensorMapElementType",
        "interleave": "TensorMapInterleaveLayout",
        "swizzle": "TensorMapSwizzleMode",
        "atomicity": "TensorMapSwizzleAtomicity",
        "fill": "TensorMapFillMode",
    }
    typ = ("TensorKnownProjectedElement" if name == "element" else
           f"TensorKnownProjectedField<{types[name]}>")
    lines = [f"f.{name} = {typ}{{}};"]
    if claim.identity is not None:
        member = ("B6x16P32OrB6p2x16" if name == "element" and
                  claim.identity in ("b6x16_p32", "b6p2x16") else
                  _cpp_pascal(claim.identity))
        lines.append(f"f.{name}->value = {types[name]}::{member};")
    elif name == "element" and claim.code == 15:
        lines.append("f.element->value = TensorMapElementType::B6x16P32OrB6p2x16;")
    if claim.code is not None:
        lines.append(f"f.{name}->code = {_cpp_unsigned(claim.code)};")
    if claim.valid is not None:
        lines.append(f"f.{name}->valid = {'true' if claim.valid else 'false'};")
    if name == "element" and claim.code == 15 and claim.identity in (
            "b6x16_p32", "b6p2x16"):
        meaning = "Load" if claim.identity == "b6x16_p32" else "Store"
        lines.append("f.element->code15_interpretation = "
                     f"TensorKnownCode15Interpretation::{meaning};")
    return lines


def _cpp_fixture_input(selected: TensorAccessContext,
                       supplied: TensorMapKnownFacts) -> list[str]:
    """Render value-identical representable context and fact records."""

    lines = ["TensorKnownAccessContext c;", "TensorMapKnownFacts f;"]
    lines += [
        f"c.direction = TensorKnownFactDirection::{_cpp_pascal(selected.direction.name)};",
        f"c.mode = TensorAccessMode::{_cpp_pascal(selected.mode.name)};",
        f"c.rank = static_cast<TensorRank>({selected.rank});",
    ]
    if selected.destination is not None:
        lines.append("c.destination = TensorKnownFactDestination::" +
                     _cpp_pascal(selected.destination.name) + ";")
    if selected.target_identity is not None:
        lines.extend((
            f'auto target = base::find_target_profile("{selected.target_identity}");',
            "ASSERT_TRUE(target.has_value());",
            "c.target_identity = target->identity;",
        ))
    if selected.reduction_op is not None:
        lines.append("c.reduction_op = TensorReductionOp::" +
                     _cpp_pascal(selected.reduction_op.name) + ";")
    for name in ("selected_variant_available", "selected_value_available",
                 "active_atomicity_use"):
        value = getattr(selected, name)
        if value is not None:
            lines.append(f"c.{name} = {'true' if value else 'false'};")
    if selected.group is not None:
        lines.append("c.group = TensorCtaGroup::" +
                     ("One" if selected.group == 1 else "Two") + ";")
    for name, entries in (("coordinates", selected.coordinates),
                          ("info", selected.info)):
        if entries is None:
            continue
        if name == "info":
            lines.append("c.info_known = true;")
        lines.append(f"c.{ 'coordinate' if name == 'coordinates' else 'info' }_arity = {len(entries)};")
        for index, entry in enumerate(entries):
            if entry.value is not None:
                lines.append(f"c.{name}[{index}].value = {entry.value};")
            if entry.source_bits is not None:
                lines.append(f"c.{name}[{index}].source_bits = " +
                             _cpp_unsigned(entry.source_bits) + ";")
    if supplied.rank is not None:
        lines.append(f"f.rank = static_cast<TensorRank>({supplied.rank});")
    for name in ("element", "interleave", "swizzle", "atomicity", "fill"):
        claim = getattr(supplied, name)
        if claim is not None:
            lines.extend(_cpp_projected(name, claim))
    signed = {"im2col_lower_edge_offsets", "im2col_upper_edge_offsets"}
    for item in dataclass_fields(TensorMapKnownFacts):
        name = item.name
        if name in ("rank", "element", "interleave", "swizzle", "atomicity",
                    "fill", "reduction_interpretation"):
            continue
        value = getattr(supplied, name)
        if value is None:
            continue
        if isinstance(value, tuple):
            typ = ("TensorKnownSignedArray" if name in signed else
                   "TensorKnownUnsignedArray")
            lines.append(f"f.{name} = {typ}{{}};")
            lines.append(f"f.{name}->arity = {len(value)};")
            for index, entry in enumerate(value):
                if entry is not None:
                    literal = str(entry) if name in signed else _cpp_unsigned(entry)
                    lines.append(f"f.{name}->values[{index}] = {literal};")
        else:
            lines.append(f"f.{name} = {value};")
    if supplied.reduction_interpretation is not None:
        lines.append("f.reduction_interpretation = base::ScalarType::" +
                     _cpp_pascal(supplied.reduction_interpretation).upper() + ";")
    return lines


def render_cpp_known_fact_parity_fixtures() -> str:
    """Prepare C++ assertions from independent literal Python fixture inputs.

    This text is test-only, generated as a private include outside the
    production plan. Its 134 expected cells retain all 128 baseline inputs,
    correct three contradictory-use outcomes, and add three valid N/A controls
    plus three reduction boundaries.
    """

    blocks = ["/** Compare literal cross-language rule expectations. */",
              "TEST(TensorMapKnownFacts, GeneratedLiteralParityMatrix) {"]
    for label, rule, selected, supplied, expected in _matrix_cases():
        escaped = label.replace("\\", "\\\\").replace('"', '\\"')
        blocks.extend(("  {", f'    SCOPED_TRACE("{escaped}");'))
        blocks.extend("    " + line for line in _cpp_fixture_input(selected, supplied))
        blocks.extend((
            "    const auto report = validate_tensor_access_facts(c, f);",
            f"    const auto* item = outcome(report, {rule.value});",
            "    ASSERT_NE(item, nullptr);",
            "    EXPECT_EQ(item->status, TensorKnownFactStatus::" +
            _cpp_pascal(expected.name) + ");",
            "  }",
        ))
    blocks.append("}")
    blocks.extend((
        "/** Diagnose typed malformed analogs without losing independent rows. */",
        "TEST(TensorMapKnownFacts, GeneratedMalformedParityCases) {",
    ))
    malformed = TensorMapKnownFactsTests()._malformed_cases()
    if len(malformed) != 19:
        raise ValueError("malformed fixture inventory must contain 19 cases")
    for label, selected, supplied, diagnostic, rule in malformed:
        if label in ("invalid availability claim", "invalid atomicity use"):
            field_name = ("selected_value_available" if label ==
                          "invalid availability claim" else "active_atomicity_use")
            blocks.extend((
                "  static_assert(!std::is_assignable_v<",
                f"      decltype(TensorKnownAccessContext{{}}.{field_name})&,",
                "      std::string>);",
            ))
            continue
        typed_selected, typed_supplied = selected, supplied
        fixups: tuple[str, ...] = ()
        if label == "invalid direction":
            typed_selected = replace(selected, direction=TensorDirection.LOAD)
            fixups = ("c.direction = static_cast<TensorKnownFactDirection>(255);",)
        elif label == "unhashable mode":
            typed_selected = replace(selected, mode=TensorAccessMode.TILED)
            fixups = ("c.mode = static_cast<TensorAccessMode>(255);",)
        elif label == "invalid projection code":
            typed_supplied = replace(supplied, element=field("u32", 2))
            fixups = ("f.element->code = UINT64_MAX;",)
        elif label == "invalid reduction scalar enum":
            typed_supplied = replace(supplied, reduction_interpretation="u32")
            fixups = (
                "f.reduction_interpretation = static_cast<base::ScalarType>(255);",
            )
        elif label == "invalid coordinate container":
            typed_selected = replace(selected, coordinates=context().coordinates)
            fixups = ("c.coordinate_arity = 6;",)
        elif label == "invalid coordinate source bits":
            typed_selected = replace(selected, coordinates=context().coordinates)
            fixups = ("c.coordinates[0].source_bits = 1;",)
        elif label == "invalid info container":
            typed_selected = replace(selected, info=None)
            fixups = ("c.info_known = true;", "c.info_arity = 4;")
        elif label == "invalid info use value":
            typed_selected = replace(selected, info=(ConvertedInteger(0),))
            fixups = ("c.info[0].value = 65536;",)
        escaped = label.replace("\\", "\\\\").replace('"', '\\"')
        blocks.extend(("  {", f'    SCOPED_TRACE("{escaped}");'))
        blocks.extend("    " + line for line in _cpp_fixture_input(
            typed_selected, typed_supplied))
        blocks.extend("    " + line for line in fixups)
        blocks.extend((
            "    const auto report = validate_tensor_access_facts(c, f);",
            "    EXPECT_TRUE(std::any_of(report.diagnostics.begin(),",
            "        report.diagnostics.end(), [](const std::string& text) {",
            f'      return text.find("{diagnostic}") != std::string::npos;',
            "    }));",
            f"    const auto* item = outcome(report, {rule.value});",
            "    ASSERT_NE(item, nullptr);",
            "    EXPECT_EQ(item->status, TensorKnownFactStatus::Unresolved);",
            *(('    for (const auto& outcome : report.outcomes)',
               '      EXPECT_EQ(outcome.status, TensorKnownFactStatus::Unresolved);')
              if label.startswith("isolated ") else ()),
            "  }",
        ))
    blocks.append("}")
    return "\n".join(blocks) + "\n"


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--emit-cpp-fixtures":
        destination = Path(sys.argv[2])
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(render_cpp_known_fact_parity_fixtures(),
                               encoding="utf-8")
    else:
        unittest.main()
