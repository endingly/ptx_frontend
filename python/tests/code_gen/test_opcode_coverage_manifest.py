from copy import deepcopy
from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from code_gen.database import load_codegen_database
from code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / "instructions/opcode_coverage.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/opcode-coverage-v2.schema.yaml"
SPEC_DIR = REPO_ROOT / "instructions/ptx_spec"

M9_OPCODE_ISSUES = {
    "ret": ("M9-I03",),
    "exit": ("M9-I04",),
    "trap": ("M9-I05",),
    "and": ("M9-I06",),
    "or": ("M9-I07",),
    "xor": ("M9-I08",),
    "not": ("M9-I09",),
    "shl": ("M9-I10",),
    "shr": ("M9-I11",),
    "setp": ("M9-I14",),
    "selp": ("M9-I15",),
    "cvt": ("M9-I16", "M9-I17", "M9-I18"),
    "cvta": ("M9-I19",),
    "mul": ("M9-I20", "M9-I21"),
    "mad": ("M9-I22",),
    "fma": ("M9-I23",),
    "div": ("M9-I24",),
}


def source_variant_sections() -> dict[tuple[str, str], str]:
    sections = {}
    for path in sorted(SPEC_DIR.rglob("*.yaml")):
        source = load_yaml(path)
        for instruction in source["instructions"]:
            for variant in instruction["variants"]:
                sections[(instruction["opcode"], variant["name"])] = variant.get(
                    "section", instruction["section"]
                )
    return sections


class OpcodeCoverageManifestTests(unittest.TestCase):
    def test_slice_pattern_fields_reject_non_strings(self) -> None:
        manifest = load_yaml(MANIFEST)
        validator = Draft202012Validator(load_yaml(SCHEMA))
        for field in ("id", "section", "spec_variant", "operand_layout"):
            for value in (0, None, {}, []):
                with self.subTest(field=field, value=value):
                    document = deepcopy(manifest)
                    document["opcodes"][0]["slices"][0][field] = value
                    self.assertTrue(list(validator.iter_errors(document)))

    def test_matches_current_database_m9_plan_and_frozen_slices(self) -> None:
        manifest = load_yaml(MANIFEST)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        entries = manifest["opcodes"]
        opcodes = [entry["opcode"] for entry in entries]
        self.assertEqual(len(opcodes), 68)
        self.assertEqual(len(opcodes), len(set(opcodes)))

        by_opcode = {entry["opcode"]: entry for entry in entries}
        database = load_codegen_database(spec_dir=SPEC_DIR)
        database_opcodes = {instruction.opcode for instruction in database.instructions}
        self.assertEqual(set(by_opcode), database_opcodes | set(M9_OPCODE_ISSUES))

        slices = [slice_ for entry in entries for slice_ in entry["slices"]]
        self.assertEqual(len(slices), 311)
        self.assertEqual(len({slice_["id"] for slice_ in slices}), len(slices))
        self.assertEqual({slice_["disposition"] for slice_ in slices}, {"implemented"})
        sections = source_variant_sections()
        database_keys = {
            (instruction.opcode, sections[(instruction.opcode, variant.name)],
             variant.name, layout.name)
            for instruction in database.instructions
            for variant in instruction.variants
            for layout in variant.operand_layouts
        }
        manifest_keys = {
            (entry["opcode"], slice_["section"], slice_["spec_variant"],
             slice_["operand_layout"])
            for entry in entries
            for slice_ in entry["slices"]
        }
        self.assertEqual(manifest_keys, database_keys)
        self.assertEqual(
            {slice_["id"] for slice_ in slices},
            {
                f'{instruction.opcode}-{variant.name.replace("_", "-")}-'
                f'{layout.name.replace("_", "-")}'
                for instruction in database.instructions
                for variant in instruction.variants
                for layout in variant.operand_layouts
            },
        )
        self.assertTrue(
            all(
                slice_["id"] ==
                f'{entry["opcode"]}-{slice_["spec_variant"].replace("_", "-")}-'
                f'{slice_["operand_layout"].replace("_", "-")}'
                for entry in entries
                for slice_ in entry["slices"]
            )
        )

        for opcode in database_opcodes:
            self.assertEqual(
                {field: by_opcode[opcode]["status"][field]
                 for field in ("syntax", "resolved", "checker")},
                {"syntax": "partial", "resolved": "partial", "checker": "partial"},
            )
            self.assertEqual(by_opcode[opcode]["status"]["simulator"], "unsupported")
            self.assertTrue(by_opcode[opcode]["slices"])
            self.assertTrue(
                all(
                    slice_["status"] == {
                        "syntax": "supported",
                        "resolved": "supported",
                        "checker": "supported",
                        "simulator": "unsupported",
                    }
                    for slice_ in by_opcode[opcode]["slices"]
                )
            )
            if opcode not in M9_OPCODE_ISSUES:
                self.assertNotIn("m9_issues", by_opcode[opcode])

        for opcode, issues in M9_OPCODE_ISSUES.items():
            expected_frontend_status = (
                "partial" if opcode in database_opcodes else "unsupported"
            )
            self.assertEqual(
                by_opcode[opcode]["status"],
                {
                    "syntax": expected_frontend_status,
                    "resolved": expected_frontend_status,
                    "checker": expected_frontend_status,
                    "simulator": "unsupported",
                },
            )
            self.assertEqual(tuple(by_opcode[opcode]["m9_issues"]), issues)

        selectors = {slice_["id"]: slice_["selector"] for slice_ in slices}
        self.assertEqual(
            {slice_id: selectors[f"{slice_id}-default"] for slice_id in (
                "mul-mul-hi-u32", "mul-mul-lo-u32", "mul-mul-rn-f32",
                "mul-mul-wide-u32", "mul-mul-wide-s32", "mad-mad-lo-s32", "mad-mad-rn-f32",
                "mad-mad-wide-u32", "fma-fma-rn-f16", "fma-fma-rn-f64",
                "div-div-rn-f32", "div-div-rn-f64", "div-div-s32",
                "rem-rem-s32", "rem-rem-u32",
                "min-min-s32", "min-min-nan-f32",
                "max-max-s32", "max-max-nan-f32",
                "abs-abs-s32", "abs-abs-f32",
                "neg-neg-s32", "neg-neg-f32", "neg-neg-f16x2",
                "lop3-lop3-b32",
                "shf-shf-l-clamp-b32", "shf-shf-r-wrap-b32",
                "prmt-prmt-generic-b32", "prmt-prmt-f4e-b32",
                "popc-popc-b32",
                "clz-clz-b32", "clz-clz-b64",
                "bfind-bfind-shiftamt-u32",
                "bfe-bfe-u32",
                "bfi-bfi-b32",
                "brev-brev-b32",
                "cvt-cvt-s32-u32",
                "cvt-cvt-rn-f32-f64", "cvt-cvt-rn-f32-u32", "cvt-cvt-rn-f32-s32", "cvt-cvt-rzi-u32-f32", "cvt-cvt-rn-f16x2-f32", "cvt-cvt-pack-sat-u8-s32-b32",
                "isspacep-isspacep-global-u64",
                "ld-ld-global-nc-l1-no-allocate-u32",
                "prefetchu-prefetchu-l1",
                "createpolicy-createpolicy-fractional-l2-evict-last-b64",
                "applypriority-applypriority-global-l2-evict-normal",
                "discard-discard-global-l2",
                "setmaxnreg-setmaxnreg-inc-sync-aligned-u32",
                "set-set-eq-u32-u32", "set-set-lt-and-f32-s32",
                "setp-setp-ge-s32",
                "ld-ld-generic-scalar", "ld-ld-generic-vector", "ld-ld-global-u32-l1-evict",
                "ld-ld-explicit-vector", "st-st-generic-scalar", "st-st-generic-vector",
                "st-st-global-u32-l2-cache-hint", "st-st-explicit-vector",
                "cp-cp-async-ca-shared-global", "cp-cp-async-commit-group",
                "cp-cp-async-wait-group", "cp-cp-async-wait-all",
                "ldmatrix-ldmatrix-sync-aligned-m8n8-x2-shared-b16",
                "mma-mma-sync-aligned-m16n8k8-row-col-f32-f16-f16-f32",
            )},
            {
                "mul-mul-lo-u32": {"topology": "arithmetic", "types": ["u32"], "shape": "scalar", "modifiers": ["lo"]},
                "mul-mul-rn-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar", "modifiers": ["rn"]},
                "mul-mul-hi-u32": {"topology": "arithmetic", "types": ["u32"], "shape": "scalar", "modifiers": ["hi"]},
                "mul-mul-wide-u32": {"topology": "arithmetic", "types": ["u64", "u32"], "shape": "scalar", "modifiers": ["wide"]},
                "mul-mul-wide-s32": {"topology": "arithmetic", "types": ["s64", "s32"], "shape": "scalar", "modifiers": ["wide"]},
                "setp-setp-ge-s32": {"topology": "comparison", "types": ["s32"], "shape": "scalar", "modifiers": ["ge"]},
                "mad-mad-lo-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar", "modifiers": ["lo"]},
                "mad-mad-rn-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar", "modifiers": ["rn"]},
                "mad-mad-wide-u32": {"topology": "arithmetic", "types": ["u64", "u32"], "shape": "scalar", "modifiers": ["wide"]},
                "fma-fma-rn-f16": {"topology": "arithmetic", "types": ["f16"], "shape": "scalar", "modifiers": ["rn"]},
                "fma-fma-rn-f64": {"topology": "arithmetic", "types": ["f64"], "shape": "scalar", "modifiers": ["rn"]},
                "div-div-rn-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar", "modifiers": ["rn"]},
                "div-div-rn-f64": {"topology": "arithmetic", "types": ["f64"], "shape": "scalar", "modifiers": ["rn"]},
                "div-div-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar"},
                "rem-rem-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar"},
                "rem-rem-u32": {"topology": "arithmetic", "types": ["u32"], "shape": "scalar"},
                "min-min-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar"},
                "min-min-nan-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar", "modifiers": ["nan"]},
                "max-max-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar"},
                "max-max-nan-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar", "modifiers": ["nan"]},
                "abs-abs-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar"},
                "abs-abs-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar"},
                "neg-neg-s32": {"topology": "arithmetic", "types": ["s32"], "shape": "scalar"},
                "neg-neg-f32": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar"},
                "neg-neg-f16x2": {"topology": "arithmetic", "types": ["f16x2"], "shape": "scalar"},
                "lop3-lop3-b32": {"topology": "logic", "types": ["b32"], "shape": "scalar"},
                "shf-shf-l-clamp-b32": {"topology": "logic", "types": ["b32"], "shape": "scalar", "modifiers": ["l", "clamp"]},
                "shf-shf-r-wrap-b32": {"topology": "logic", "types": ["b32"], "shape": "scalar", "modifiers": ["r", "wrap"]},
                "prmt-prmt-generic-b32": {"topology": "data_movement", "types": ["b32"], "shape": "scalar"},
                "prmt-prmt-f4e-b32": {"topology": "data_movement", "types": ["b32"], "shape": "scalar", "modifiers": ["f4e"]},
                "popc-popc-b32": {"topology": "arithmetic", "types": ["u32", "b32"], "shape": "scalar"},
                "clz-clz-b32": {"topology": "arithmetic", "types": ["u32", "b32"], "shape": "scalar"},
                "clz-clz-b64": {"topology": "arithmetic", "types": ["u32", "b64"], "shape": "scalar"},
                "bfind-bfind-shiftamt-u32": {"topology": "arithmetic", "types": ["u32"], "shape": "scalar", "modifiers": ["shiftamt"]},
                "bfe-bfe-u32": {"topology": "arithmetic", "types": ["u32"], "shape": "scalar"},
                "bfi-bfi-b32": {"topology": "arithmetic", "types": ["b32"], "shape": "scalar"},
                "brev-brev-b32": {"topology": "arithmetic", "types": ["b32"], "shape": "scalar"},
                "cvt-cvt-s32-u32": {"topology": "conversion", "types": ["s32", "u32"], "shape": "scalar"},
                "cvt-cvt-rn-f32-f64": {"topology": "conversion", "types": ["f32", "f64"], "shape": "scalar", "modifiers": ["rn"]},
                "cvt-cvt-rn-f32-u32": {"topology": "conversion", "types": ["f32", "u32"], "shape": "scalar", "modifiers": ["rn"]},
                "cvt-cvt-rn-f32-s32": {"topology": "conversion", "types": ["f32", "s32"], "shape": "scalar", "modifiers": ["rn"]},
                "cvt-cvt-rzi-u32-f32": {"topology": "conversion", "types": ["u32", "f32"], "shape": "scalar", "modifiers": ["rzi"]},
                "cvt-cvt-rn-f16x2-f32": {"topology": "conversion", "types": ["f16x2", "f32"], "shape": "scalar", "modifiers": ["rn"]},
                "cvt-cvt-pack-sat-u8-s32-b32": {"topology": "conversion", "types": ["u8", "s32", "b32"], "shape": "scalar", "modifiers": ["pack", "sat"]},
                "isspacep-isspacep-global-u64": {"topology": "data_movement", "types": ["pred", "u64"], "shape": "scalar", "state_space": ["global"]},
                "ld-ld-global-nc-l1-no-allocate-u32": {"topology": "memory", "types": ["u32"], "shape": "scalar", "modifiers": ["nc", "l1_no_allocate"], "state_space": ["global"]},
                "prefetchu-prefetchu-l1": {"topology": "memory", "types": [], "shape": "address", "modifiers": ["l1"], "state_space": ["generic"]},
                "createpolicy-createpolicy-fractional-l2-evict-last-b64": {"topology": "data_movement", "types": ["b64", "f32"], "shape": "scalar", "modifiers": ["fractional", "l2_evict_last"]},
                "applypriority-applypriority-global-l2-evict-normal": {"topology": "data_movement", "types": ["u32"], "shape": "address", "modifiers": ["l2_evict_normal"], "state_space": ["global"]},
                "discard-discard-global-l2": {"topology": "data_movement", "types": ["u32"], "shape": "address", "modifiers": ["l2"], "state_space": ["global"]},
                "setmaxnreg-setmaxnreg-inc-sync-aligned-u32": {"topology": "control", "types": ["u32"], "shape": "immediate", "modifiers": ["inc", "sync", "aligned"]},
                "set-set-eq-u32-u32": {"topology": "comparison", "types": ["u32"], "shape": "scalar", "modifiers": ["eq"]},
                "set-set-lt-and-f32-s32": {"topology": "comparison", "types": ["f32", "s32"], "shape": "scalar", "modifiers": ["lt", "and"]},
                "ld-ld-generic-scalar": {"topology": "memory", "types": ["b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"], "shape": "scalar", "state_space": ["const", "global", "local", "param", "shared"]},
                "ld-ld-generic-vector": {"topology": "memory", "types": ["b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"], "shape": "vector_legacy", "state_space": ["const", "global", "local", "param", "shared"]},
                "ld-ld-global-u32-l1-evict": {"topology": "memory", "types": ["u32"], "shape": "scalar", "modifiers": ["l1_evict"], "state_space": ["global"]},
                "ld-ld-explicit-vector": {"topology": "memory", "types": ["b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"], "shape": "vector_256", "modifiers": ["memory_order"], "state_space": ["global"]},
                "st-st-generic-scalar": {"topology": "memory", "types": ["b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"], "shape": "scalar", "state_space": ["global", "local", "param", "shared"]},
                "st-st-generic-vector": {"topology": "memory", "types": ["b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"], "shape": "vector_legacy", "state_space": ["global", "local", "param", "shared"]},
                "st-st-global-u32-l2-cache-hint": {"topology": "memory", "types": ["u32"], "shape": "scalar", "modifiers": ["l2_cache_hint"], "state_space": ["global"]},
                "st-st-explicit-vector": {"topology": "memory", "types": ["b8", "b16", "b32", "b64", "u8", "u16", "u32", "u64", "s8", "s16", "s32", "s64", "f32", "f64"], "shape": "vector_256", "modifiers": ["memory_order"], "state_space": ["global"]},
                "cp-cp-async-ca-shared-global": {"topology": "async_copy", "types": ["b32"], "shape": "scalar", "modifiers": ["async", "ca"], "state_space": ["shared", "global"]},
                "cp-cp-async-commit-group": {"topology": "async_group", "types": [], "shape": "none", "modifiers": ["commit_group"]},
                "cp-cp-async-wait-group": {"topology": "async_group", "types": ["u32"], "shape": "immediate", "modifiers": ["wait_group"]},
                "cp-cp-async-wait-all": {"topology": "async_group", "types": [], "shape": "none", "modifiers": ["wait_all"]},
                "ldmatrix-ldmatrix-sync-aligned-m8n8-x2-shared-b16": {"topology": "matrix_load", "types": ["b16", "b32"], "shape": "m8n8_x2", "modifiers": ["sync", "aligned"], "state_space": ["shared"]},
                "mma-mma-sync-aligned-m16n8k8-row-col-f32-f16-f16-f32": {"topology": "matrix_mma", "types": ["f32", "f16", "f16x2"], "shape": "m16n8k8", "modifiers": ["sync", "aligned", "row", "col"]},
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "match-match-any-sync-default",
                    "match-match-all-sync-without-predicate",
                    "match-match-all-sync-with-predicate",
                )
            },
            {
                "match-match-any-sync-default": {
                    "topology": "warp_match", "types": ["b32", "b64"],
                    "shape": "scalar", "modifiers": ["any", "sync"],
                },
                "match-match-all-sync-without-predicate": {
                    "topology": "warp_match", "types": ["b32", "b64"],
                    "shape": "scalar", "modifiers": ["all", "sync"],
                },
                "match-match-all-sync-with-predicate": {
                    "topology": "warp_match", "types": ["b32", "b64"],
                    "shape": "predicate_pair", "modifiers": ["all", "sync"],
                },
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "mbarrier-mbarrier-init-generic-v0-default",
                    "mbarrier-mbarrier-init-shared-v0-default",
                    "mbarrier-mbarrier-init-shared-cta-v0-default",
                    "mbarrier-mbarrier-init-generic-v1-default",
                    "mbarrier-mbarrier-init-shared-v1-default",
                    "mbarrier-mbarrier-init-shared-cta-v1-default",
                    "mbarrier-mbarrier-inval-generic-default",
                    "mbarrier-mbarrier-inval-shared-default",
                    "mbarrier-mbarrier-inval-shared-cta-default",
                    "mbarrier-mbarrier-expect-tx-generic-or-shared-default",
                    "mbarrier-mbarrier-expect-tx-shared-cta-default",
                    "mbarrier-mbarrier-expect-tx-shared-cluster-default",
                    "mbarrier-mbarrier-expect-tx-relaxed-cta-generic-or-shared-default",
                    "mbarrier-mbarrier-expect-tx-relaxed-cta-shared-cta-default",
                    "mbarrier-mbarrier-expect-tx-relaxed-cta-shared-cluster-default",
                    "mbarrier-mbarrier-expect-tx-relaxed-cluster-generic-or-shared-default",
                    "mbarrier-mbarrier-expect-tx-relaxed-cluster-shared-cta-default",
                    "mbarrier-mbarrier-expect-tx-relaxed-cluster-shared-cluster-default",
                    "mbarrier-mbarrier-complete-tx-generic-or-shared-default",
                    "mbarrier-mbarrier-complete-tx-shared-cta-default",
                    "mbarrier-mbarrier-complete-tx-shared-cluster-default",
                    "mbarrier-mbarrier-complete-tx-relaxed-cta-generic-or-shared-default",
                    "mbarrier-mbarrier-complete-tx-relaxed-cta-shared-cta-default",
                    "mbarrier-mbarrier-complete-tx-relaxed-cta-shared-cluster-default",
                    "mbarrier-mbarrier-complete-tx-relaxed-cluster-generic-or-shared-default",
                    "mbarrier-mbarrier-complete-tx-relaxed-cluster-shared-cta-default",
                    "mbarrier-mbarrier-complete-tx-relaxed-cluster-shared-cluster-default",
                )
            },
            {
                "mbarrier-mbarrier-init-generic-v0-default": {
                    "topology": "mbarrier_init", "types": ["b64", "u32"],
                    "shape": "shared_address_and_count",
                    "modifiers": ["init", "layout_v0"],
                },
                "mbarrier-mbarrier-init-shared-v0-default": {
                    "topology": "mbarrier_init", "types": ["b64", "u32"],
                    "shape": "shared_address_and_count",
                    "modifiers": ["init", "layout_v0", "shared"],
                },
                "mbarrier-mbarrier-init-shared-cta-v0-default": {
                    "topology": "mbarrier_init", "types": ["b64", "u32"],
                    "shape": "shared_address_and_count",
                    "modifiers": ["init", "layout_v0", "shared_cta"],
                },
                "mbarrier-mbarrier-init-generic-v1-default": {
                    "topology": "mbarrier_init", "types": ["b64", "u32"],
                    "shape": "shared_address_and_count",
                    "modifiers": ["init", "layout_v1"],
                },
                "mbarrier-mbarrier-init-shared-v1-default": {
                    "topology": "mbarrier_init", "types": ["b64", "u32"],
                    "shape": "shared_address_and_count",
                    "modifiers": ["init", "layout_v1", "shared"],
                },
                "mbarrier-mbarrier-init-shared-cta-v1-default": {
                    "topology": "mbarrier_init", "types": ["b64", "u32"],
                    "shape": "shared_address_and_count",
                    "modifiers": ["init", "layout_v1", "shared_cta"],
                },
                "mbarrier-mbarrier-inval-generic-default": {
                    "topology": "mbarrier_inval", "types": ["b64"],
                    "shape": "shared_address", "modifiers": ["inval"],
                },
                "mbarrier-mbarrier-inval-shared-default": {
                    "topology": "mbarrier_inval", "types": ["b64"],
                    "shape": "shared_address", "modifiers": ["inval", "shared"],
                },
                "mbarrier-mbarrier-inval-shared-cta-default": {
                    "topology": "mbarrier_inval", "types": ["b64"],
                    "shape": "shared_address",
                    "modifiers": ["inval", "shared_cta"],
                },
                "mbarrier-mbarrier-expect-tx-generic-or-shared-default": {
                    "topology": "mbarrier_expect_tx", "types": ["b64", "u32"],
                    "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "generic_or_shared"],
                },
                "mbarrier-mbarrier-expect-tx-shared-cta-default": {
                    "topology": "mbarrier_expect_tx", "types": ["b64", "u32"],
                    "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "shared_cta"],
                },
                "mbarrier-mbarrier-expect-tx-shared-cluster-default": {
                    "topology": "mbarrier_expect_tx", "types": ["b64", "u32"],
                    "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "shared_cluster"],
                },
                "mbarrier-mbarrier-expect-tx-relaxed-cta-generic-or-shared-default": {
                    "topology": "mbarrier_expect_tx_relaxed_cta",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "relaxed", "cta", "generic_or_shared"],
                },
                "mbarrier-mbarrier-expect-tx-relaxed-cta-shared-cta-default": {
                    "topology": "mbarrier_expect_tx_relaxed_cta",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "relaxed", "cta", "shared_cta"],
                },
                "mbarrier-mbarrier-expect-tx-relaxed-cta-shared-cluster-default": {
                    "topology": "mbarrier_expect_tx_relaxed_cta",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "relaxed", "cta", "shared_cluster"],
                },
                "mbarrier-mbarrier-expect-tx-relaxed-cluster-generic-or-shared-default": {
                    "topology": "mbarrier_expect_tx_relaxed_cluster",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "relaxed", "cluster", "generic_or_shared"],
                },
                "mbarrier-mbarrier-expect-tx-relaxed-cluster-shared-cta-default": {
                    "topology": "mbarrier_expect_tx_relaxed_cluster",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "relaxed", "cluster", "shared_cta"],
                },
                "mbarrier-mbarrier-expect-tx-relaxed-cluster-shared-cluster-default": {
                    "topology": "mbarrier_expect_tx_relaxed_cluster",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["expect_tx", "relaxed", "cluster", "shared_cluster"],
                },
                "mbarrier-mbarrier-complete-tx-generic-or-shared-default": {
                    "topology": "mbarrier_complete_tx", "types": ["b64", "u32"],
                    "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "generic_or_shared"],
                },
                "mbarrier-mbarrier-complete-tx-shared-cta-default": {
                    "topology": "mbarrier_complete_tx", "types": ["b64", "u32"],
                    "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "shared_cta"],
                },
                "mbarrier-mbarrier-complete-tx-shared-cluster-default": {
                    "topology": "mbarrier_complete_tx", "types": ["b64", "u32"],
                    "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "shared_cluster"],
                },
                "mbarrier-mbarrier-complete-tx-relaxed-cta-generic-or-shared-default": {
                    "topology": "mbarrier_complete_tx_relaxed_cta",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "relaxed", "cta", "generic_or_shared"],
                },
                "mbarrier-mbarrier-complete-tx-relaxed-cta-shared-cta-default": {
                    "topology": "mbarrier_complete_tx_relaxed_cta",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "relaxed", "cta", "shared_cta"],
                },
                "mbarrier-mbarrier-complete-tx-relaxed-cta-shared-cluster-default": {
                    "topology": "mbarrier_complete_tx_relaxed_cta",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "relaxed", "cta", "shared_cluster"],
                },
                "mbarrier-mbarrier-complete-tx-relaxed-cluster-generic-or-shared-default": {
                    "topology": "mbarrier_complete_tx_relaxed_cluster",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "relaxed", "cluster", "generic_or_shared"],
                },
                "mbarrier-mbarrier-complete-tx-relaxed-cluster-shared-cta-default": {
                    "topology": "mbarrier_complete_tx_relaxed_cluster",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "relaxed", "cluster", "shared_cta"],
                },
                "mbarrier-mbarrier-complete-tx-relaxed-cluster-shared-cluster-default": {
                    "topology": "mbarrier_complete_tx_relaxed_cluster",
                    "types": ["b64", "u32"], "shape": "shared_address_and_tx_count",
                    "modifiers": ["complete_tx", "relaxed", "cluster", "shared_cluster"],
                },
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "getctarank-getctarank-shared-cluster-default",
                    "getctarank-getctarank-generic-default",
                )
            },
            {
                "getctarank-getctarank-shared-cluster-default": {
                    "topology": "cluster_rank_query", "types": ["u32", "u64"],
                    "shape": "register_or_shared_symbol_address",
                    "modifiers": ["shared_cluster"],
                },
                "getctarank-getctarank-generic-default": {
                    "topology": "generic_rank_query", "types": ["u32", "u64"],
                    "shape": "register",
                },
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "mapa-mapa-shared-cluster-default",
                    "mapa-mapa-generic-default",
                )
            },
            {
                "mapa-mapa-shared-cluster-default": {
                    "topology": "cluster_address_mapping", "types": ["u32", "u64"],
                    "shape": "register_or_shared_symbol_address",
                    "modifiers": ["shared_cluster"],
                },
                "mapa-mapa-generic-default": {
                    "topology": "generic_address_mapping", "types": ["u32", "u64"],
                    "shape": "register",
                },
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "redux-redux-sync-add-default",
                    "redux-redux-sync-min-default",
                    "redux-redux-sync-max-default",
                    "redux-redux-sync-boolean-default",
                    "redux-redux-sync-min-f32-default",
                    "redux-redux-sync-max-f32-default",
                )
            },
            {
                "redux-redux-sync-add-default": {
                    "topology": "warp_reduce", "types": ["u32", "s32"],
                    "shape": "scalar", "modifiers": ["sync", "add"],
                },
                "redux-redux-sync-min-default": {
                    "topology": "warp_reduce", "types": ["u32", "s32"],
                    "shape": "scalar", "modifiers": ["sync", "min"],
                },
                "redux-redux-sync-max-default": {
                    "topology": "warp_reduce", "types": ["u32", "s32"],
                    "shape": "scalar", "modifiers": ["sync", "max"],
                },
                "redux-redux-sync-boolean-default": {
                    "topology": "warp_reduce", "types": ["b32"],
                    "shape": "scalar", "modifiers": ["sync", "and", "or", "xor"],
                },
                "redux-redux-sync-min-f32-default": {
                    "topology": "warp_reduce", "types": ["f32"],
                    "shape": "scalar", "modifiers": ["sync", "min", "abs", "nan"],
                },
                "redux-redux-sync-max-f32-default": {
                    "topology": "warp_reduce", "types": ["f32"],
                    "shape": "scalar", "modifiers": ["sync", "max", "abs", "nan"],
                },
            },
        )
        self.assertEqual(
            selectors["elect-elect-sync-default"],
            {
                "topology": "warp_election", "types": ["u32", "pred"],
                "shape": "optional_data_predicate_pair", "modifiers": ["sync"],
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "fence-fence-proxy-async-default",
                    "fence-fence-proxy-async-shared-cluster-default",
                    "fence-fence-proxy-tensormap-generic-release-default",
                    "fence-fence-proxy-tensormap-generic-release-cluster-default",
                    "fence-fence-proxy-tensormap-generic-acquire-default",
                    "fence-fence-proxy-tensormap-generic-acquire-cluster-default",
                    "fence-fence-proxy-async-generic-acquire-sync-restrict-shared-cluster-default",
                    "fence-fence-proxy-async-generic-release-sync-restrict-shared-cta-default",
                )
            },
            {
                "fence-fence-proxy-async-default": {"topology": "proxy_fence_bidirectional", "types": [], "shape": "none", "modifiers": ["proxy", "async"]},
                "fence-fence-proxy-async-shared-cluster-default": {"topology": "proxy_fence_bidirectional", "types": [], "shape": "none", "modifiers": ["proxy", "async_shared_cluster"]},
                "fence-fence-proxy-tensormap-generic-release-default": {"topology": "proxy_fence_to_from_release", "types": [], "shape": "none", "modifiers": ["proxy", "tensormap_to_generic", "release", "scope_cta_gpu_sys"]},
                "fence-fence-proxy-tensormap-generic-release-cluster-default": {"topology": "proxy_fence_to_from_release", "types": [], "shape": "none", "modifiers": ["proxy", "tensormap_to_generic", "release", "cluster"]},
                "fence-fence-proxy-tensormap-generic-acquire-default": {"topology": "proxy_fence_to_from_acquire", "types": ["u32"], "shape": "global_address_size", "modifiers": ["proxy", "tensormap_to_generic", "acquire", "scope_cta_gpu_sys"]},
                "fence-fence-proxy-tensormap-generic-acquire-cluster-default": {"topology": "proxy_fence_to_from_acquire", "types": ["u32"], "shape": "global_address_size", "modifiers": ["proxy", "tensormap_to_generic", "acquire", "cluster"]},
                "fence-fence-proxy-async-generic-acquire-sync-restrict-shared-cluster-default": {"topology": "proxy_fence_to_from_sync_restrict", "types": [], "shape": "none", "modifiers": ["proxy", "async_to_generic", "acquire", "sync_restrict_shared_cluster", "cluster"]},
                "fence-fence-proxy-async-generic-release-sync-restrict-shared-cta-default": {"topology": "proxy_fence_to_from_sync_restrict", "types": [], "shape": "none", "modifiers": ["proxy", "async_to_generic", "release", "sync_restrict_shared_cta", "cluster"]},
            },
        )
        self.assertEqual(
            {
                slice_id: selectors[slice_id]
                for slice_id in (
                    "griddepcontrol-griddepcontrol-launch-dependents-default",
                    "griddepcontrol-griddepcontrol-wait-default",
                )
            },
            {
                "griddepcontrol-griddepcontrol-launch-dependents-default": {
                    "topology": "grid_dependency_control", "types": [],
                    "shape": "none", "modifiers": ["launch_dependents"],
                },
                "griddepcontrol-griddepcontrol-wait-default": {
                    "topology": "grid_dependency_control", "types": [],
                    "shape": "none", "modifiers": ["wait"],
                },
            },
        )

        expected_layouts = {
            "call": {
                ("call_direct", "target"),
                ("call_direct", "target_input"),
                ("call_direct", "return_target_input"),
                ("call_direct", "target_metadata"),
                ("call_direct", "target_input_metadata"),
                ("call_direct", "return_target_input_metadata"),
            },
            "mov": {
                ("mov_scalar", "scalar"),
                ("mov_scalar", "pack"),
                ("mov_scalar", "unpack"),
                ("mov_v4_u32", "default"),
                ("mov_pred", "default"),
            },
            "mapa": {
                ("mapa_shared_cluster", "default"),
                ("mapa_generic", "default"),
            },
            "getctarank": {
                ("getctarank_shared_cluster", "default"),
                ("getctarank_generic", "default"),
            },
            "bar": {
                ("bar_sync", "immediate_barrier"),
                ("bar_sync", "barrier"),
                ("bar_sync", "barrier_and_thread_count"),
                ("bar_cta_sync", "barrier"),
                ("bar_cta_sync", "barrier_and_thread_count"),
                ("bar_arrive", "default"),
                ("bar_cta_arrive", "default"),
                ("bar_red_popc_u32", "without_thread_count"),
                ("bar_red_popc_u32", "with_thread_count"),
                ("bar_cta_red_popc_u32", "without_thread_count"),
                ("bar_cta_red_popc_u32", "with_thread_count"),
                ("bar_red_and_pred", "without_thread_count"),
                ("bar_red_and_pred", "with_thread_count"),
                ("bar_cta_red_and_pred", "without_thread_count"),
                ("bar_cta_red_and_pred", "with_thread_count"),
                ("bar_red_or_pred", "without_thread_count"),
                ("bar_red_or_pred", "with_thread_count"),
                ("bar_cta_red_or_pred", "without_thread_count"),
                ("bar_cta_red_or_pred", "with_thread_count"),
                ("bar_warp_sync", "default"),
            },
            "barrier": {
                ("barrier_cluster_arrive", "default"),
                ("barrier_cluster_wait", "default"),
            },
            "match": {
                ("match_any_sync", "default"),
                ("match_all_sync", "without_predicate"),
                ("match_all_sync", "with_predicate"),
            },
            "redux": {
                ("redux_sync_add", "default"),
                ("redux_sync_min", "default"),
                ("redux_sync_max", "default"),
                ("redux_sync_boolean", "default"),
                ("redux_sync_min_f32", "default"),
                ("redux_sync_max_f32", "default"),
            },
            "elect": {("elect_sync", "default")},
            "griddepcontrol": {
                ("griddepcontrol_launch_dependents", "default"),
                ("griddepcontrol_wait", "default"),
            },
            "mbarrier": {
                ("mbarrier_init_generic_v0", "default"),
                ("mbarrier_init_shared_v0", "default"),
                ("mbarrier_init_shared_cta_v0", "default"),
                ("mbarrier_init_generic_v1", "default"),
                ("mbarrier_init_shared_v1", "default"),
                ("mbarrier_init_shared_cta_v1", "default"),
                ("mbarrier_inval_generic", "default"),
                ("mbarrier_inval_shared", "default"),
                ("mbarrier_inval_shared_cta", "default"),
                ("mbarrier_expect_tx_generic_or_shared", "default"),
                ("mbarrier_expect_tx_shared_cta", "default"),
                ("mbarrier_expect_tx_shared_cluster", "default"),
                ("mbarrier_expect_tx_relaxed_cta_generic_or_shared", "default"),
                ("mbarrier_expect_tx_relaxed_cta_shared_cta", "default"),
                ("mbarrier_expect_tx_relaxed_cta_shared_cluster", "default"),
                ("mbarrier_expect_tx_relaxed_cluster_generic_or_shared", "default"),
                ("mbarrier_expect_tx_relaxed_cluster_shared_cta", "default"),
                ("mbarrier_expect_tx_relaxed_cluster_shared_cluster", "default"),
                ("mbarrier_complete_tx_generic_or_shared", "default"),
                ("mbarrier_complete_tx_shared_cta", "default"),
                ("mbarrier_complete_tx_shared_cluster", "default"),
                ("mbarrier_complete_tx_relaxed_cta_generic_or_shared", "default"),
                ("mbarrier_complete_tx_relaxed_cta_shared_cta", "default"),
                ("mbarrier_complete_tx_relaxed_cta_shared_cluster", "default"),
                ("mbarrier_complete_tx_relaxed_cluster_generic_or_shared", "default"),
                ("mbarrier_complete_tx_relaxed_cluster_shared_cta", "default"),
                ("mbarrier_complete_tx_relaxed_cluster_shared_cluster", "default"),
                ("mbarrier_arrive_generic_or_shared", "no_count"),
                ("mbarrier_arrive_generic_or_shared", "with_count"),
                ("mbarrier_arrive_shared_cta", "no_count"),
                ("mbarrier_arrive_shared_cta", "with_count"),
                ("mbarrier_arrive_shared_cluster", "no_count"),
                ("mbarrier_arrive_shared_cluster", "with_count"),
                ("mbarrier_arrive_semantics_generic_or_shared", "no_count"),
                ("mbarrier_arrive_semantics_generic_or_shared", "with_count"),
                ("mbarrier_arrive_semantics_shared_cta", "no_count"),
                ("mbarrier_arrive_semantics_shared_cta", "with_count"),
                ("mbarrier_arrive_semantics_shared_cluster", "no_count"),
                ("mbarrier_arrive_semantics_shared_cluster", "with_count"),
                ("mbarrier_arrive_expect_tx_generic_or_shared", "default"),
                ("mbarrier_arrive_expect_tx_shared_cta", "default"),
                ("mbarrier_arrive_expect_tx_shared_cluster", "default"),
                ("mbarrier_arrive_expect_tx_semantics_generic_or_shared", "default"),
                ("mbarrier_arrive_expect_tx_semantics_shared_cta", "default"),
                ("mbarrier_arrive_expect_tx_semantics_shared_cluster", "default"),
                ("mbarrier_arrive_no_complete_generic_or_shared", "default"),
                ("mbarrier_arrive_no_complete_shared_cta", "default"),
                ("mbarrier_arrive_no_complete_release_cta_generic_or_shared", "default"),
                ("mbarrier_arrive_no_complete_release_cta_shared_cta", "default"),
                ("mbarrier_arrive_drop_generic_or_shared", "no_count"),
                ("mbarrier_arrive_drop_generic_or_shared", "with_count"),
                ("mbarrier_arrive_drop_shared_cta", "no_count"),
                ("mbarrier_arrive_drop_shared_cta", "with_count"),
                ("mbarrier_arrive_drop_shared_cluster", "no_count"),
                ("mbarrier_arrive_drop_shared_cluster", "with_count"),
                ("mbarrier_arrive_drop_semantics_generic_or_shared", "no_count"),
                ("mbarrier_arrive_drop_semantics_generic_or_shared", "with_count"),
                ("mbarrier_arrive_drop_semantics_shared_cta", "no_count"),
                ("mbarrier_arrive_drop_semantics_shared_cta", "with_count"),
                ("mbarrier_arrive_drop_semantics_shared_cluster", "no_count"),
                ("mbarrier_arrive_drop_semantics_shared_cluster", "with_count"),
                ("mbarrier_arrive_drop_expect_tx_generic_or_shared", "default"),
                ("mbarrier_arrive_drop_expect_tx_shared_cta", "default"),
                ("mbarrier_arrive_drop_expect_tx_shared_cluster", "default"),
                ("mbarrier_arrive_drop_expect_tx_semantics_generic_or_shared", "default"),
                ("mbarrier_arrive_drop_expect_tx_semantics_shared_cta", "default"),
                ("mbarrier_arrive_drop_expect_tx_semantics_shared_cluster", "default"),
                ("mbarrier_arrive_drop_no_complete_generic_or_shared", "default"),
                ("mbarrier_arrive_drop_no_complete_shared_cta", "default"),
                ("mbarrier_arrive_drop_no_complete_release_cta_generic_or_shared", "default"),
                ("mbarrier_arrive_drop_no_complete_release_cta_shared_cta", "default"),
                ("mbarrier_test_wait_token_generic_or_shared", "default"),
                ("mbarrier_test_wait_token_shared_cta", "default"),
                ("mbarrier_test_wait_parity_generic_or_shared", "default"),
                ("mbarrier_test_wait_parity_shared_cta", "default"),
                ("mbarrier_try_wait_token_generic_or_shared", "no_hint"),
                ("mbarrier_try_wait_token_generic_or_shared", "with_hint"),
                ("mbarrier_try_wait_token_shared_cta", "no_hint"),
                ("mbarrier_try_wait_token_shared_cta", "with_hint"),
                ("mbarrier_try_wait_parity_generic_or_shared", "no_hint"),
                ("mbarrier_try_wait_parity_generic_or_shared", "with_hint"),
                ("mbarrier_try_wait_parity_shared_cta", "no_hint"),
                ("mbarrier_try_wait_parity_shared_cta", "with_hint"),
                ("mbarrier_test_wait_token_primary_generic_or_shared", "default"),
                ("mbarrier_test_wait_token_primary_generic_or_shared", "report_predicate"),
                ("mbarrier_test_wait_token_primary_generic_or_shared", "report_predicate_value"),
                ("mbarrier_test_wait_parity_primary_generic_or_shared", "default"),
                ("mbarrier_test_wait_parity_primary_generic_or_shared", "report_predicate"),
                ("mbarrier_test_wait_parity_primary_generic_or_shared", "report_predicate_value"),
                ("mbarrier_test_wait_parity_conditional_generic_or_shared", "default"),
                ("mbarrier_test_wait_token_primary_shared_cta", "default"),
                ("mbarrier_test_wait_token_primary_shared_cta", "report_predicate"),
                ("mbarrier_test_wait_token_primary_shared_cta", "report_predicate_value"),
                ("mbarrier_test_wait_parity_primary_shared_cta", "default"),
                ("mbarrier_test_wait_parity_primary_shared_cta", "report_predicate"),
                ("mbarrier_test_wait_parity_primary_shared_cta", "report_predicate_value"),
                ("mbarrier_test_wait_parity_conditional_shared_cta", "default"),
                ("mbarrier_try_wait_token_primary_generic_or_shared", "no_hint"),
                ("mbarrier_try_wait_token_primary_generic_or_shared", "with_hint"),
                ("mbarrier_try_wait_token_primary_generic_or_shared", "report_predicate_no_hint"),
                ("mbarrier_try_wait_token_primary_generic_or_shared", "report_predicate_with_hint"),
                ("mbarrier_try_wait_token_primary_generic_or_shared", "report_predicate_value_no_hint"),
                ("mbarrier_try_wait_token_primary_generic_or_shared", "report_predicate_value_with_hint"),
                ("mbarrier_try_wait_parity_primary_generic_or_shared", "no_hint"),
                ("mbarrier_try_wait_parity_primary_generic_or_shared", "with_hint"),
                ("mbarrier_try_wait_parity_primary_generic_or_shared", "report_predicate_no_hint"),
                ("mbarrier_try_wait_parity_primary_generic_or_shared", "report_predicate_with_hint"),
                ("mbarrier_try_wait_parity_primary_generic_or_shared", "report_predicate_value_no_hint"),
                ("mbarrier_try_wait_parity_primary_generic_or_shared", "report_predicate_value_with_hint"),
                ("mbarrier_try_wait_parity_conditional_generic_or_shared", "no_hint"),
                ("mbarrier_try_wait_parity_conditional_generic_or_shared", "with_hint"),
                ("mbarrier_try_wait_token_primary_shared_cta", "no_hint"),
                ("mbarrier_try_wait_token_primary_shared_cta", "with_hint"),
                ("mbarrier_try_wait_token_primary_shared_cta", "report_predicate_no_hint"),
                ("mbarrier_try_wait_token_primary_shared_cta", "report_predicate_with_hint"),
                ("mbarrier_try_wait_token_primary_shared_cta", "report_predicate_value_no_hint"),
                ("mbarrier_try_wait_token_primary_shared_cta", "report_predicate_value_with_hint"),
                ("mbarrier_try_wait_parity_primary_shared_cta", "no_hint"),
                ("mbarrier_try_wait_parity_primary_shared_cta", "with_hint"),
                ("mbarrier_try_wait_parity_primary_shared_cta", "report_predicate_no_hint"),
                ("mbarrier_try_wait_parity_primary_shared_cta", "report_predicate_with_hint"),
                ("mbarrier_try_wait_parity_primary_shared_cta", "report_predicate_value_no_hint"),
                ("mbarrier_try_wait_parity_primary_shared_cta", "report_predicate_value_with_hint"),
                ("mbarrier_try_wait_parity_conditional_shared_cta", "no_hint"),
                ("mbarrier_try_wait_parity_conditional_shared_cta", "with_hint"),
                ("mbarrier_pending_count", "default"),
                ("mbarrier_check_layout_generic_v0", "default"),
                ("mbarrier_check_layout_generic_v1", "default"),
                ("mbarrier_check_layout_shared_cta_v0", "default"),
                ("mbarrier_check_layout_shared_cta_v1", "default"),
            },
        }
        for opcode, expected in expected_layouts.items():
            layout_slices = by_opcode[opcode]["slices"]
            self.assertEqual(
                {(slice_["spec_variant"], slice_["operand_layout"])
                 for slice_ in layout_slices},
                expected,
            )
            self.assertTrue(
                all(
                    slice_["status"] == {
                        "syntax": "supported",
                        "resolved": "supported",
                        "checker": "supported",
                        "simulator": "unsupported",
                    }
                    for slice_ in layout_slices
                )
            )

    def test_disposition_schema_conditions(self) -> None:
        validator = Draft202012Validator(load_yaml(SCHEMA))
        slice_ = {
            "id": "add-add-float-f32-default",
            "section": "9.7.3.3",
            "spec_variant": "add_float_f32",
            "operand_layout": "default",
            "selector": {"topology": "arithmetic", "types": ["f32"], "shape": "scalar"},
            "status": {
                "syntax": "supported",
                "resolved": "supported",
                "checker": "supported",
                "simulator": "unsupported",
            },
        }
        implemented = {**slice_, "disposition": "implemented"}
        self.assertEqual(list(validator.iter_errors({
            "schema": "ptx-opcode-coverage/v2", "version": 2,
            "opcodes": [{"opcode": "add", "status": implemented["status"],
                         "slices": [implemented]}],
        })), [])
        for field in ("reason", "milestone"):
            invalid = {**implemented, field: "not allowed"}
            self.assertTrue(list(validator.iter_errors({
                "schema": "ptx-opcode-coverage/v2", "version": 2,
                "opcodes": [{"opcode": "add", "status": implemented["status"],
                             "slices": [invalid]}],
            })))

        for disposition in ("planned", "deferred", "out_of_scope", "paused"):
            valid = {
                **slice_, "disposition": disposition,
                "reason": "tracked outside the current implementation",
                "milestone": "M11",
            }
            document = {
                "schema": "ptx-opcode-coverage/v2", "version": 2,
                "opcodes": [{"opcode": "add", "status": valid["status"],
                             "slices": [valid]}],
            }
            self.assertEqual(list(validator.iter_errors(document)), [])
            for field in ("reason", "milestone"):
                invalid = valid.copy()
                del invalid[field]
                self.assertTrue(list(validator.iter_errors({
                    **document, "opcodes": [{"opcode": "add", "status": valid["status"],
                                               "slices": [invalid]}],
                })))


if __name__ == "__main__":
    unittest.main()
