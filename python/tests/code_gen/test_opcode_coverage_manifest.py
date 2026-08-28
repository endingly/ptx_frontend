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
                sections[(instruction["opcode"], variant["name"])] = instruction[
                    "section"
                ]
    return sections


class OpcodeCoverageManifestTests(unittest.TestCase):
    def test_matches_current_database_m9_plan_and_frozen_slices(self) -> None:
        manifest = load_yaml(MANIFEST)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        entries = manifest["opcodes"]
        opcodes = [entry["opcode"] for entry in entries]
        self.assertEqual(len(opcodes), 38)
        self.assertEqual(len(opcodes), len(set(opcodes)))

        by_opcode = {entry["opcode"]: entry for entry in entries}
        database = load_codegen_database(spec_dir=SPEC_DIR)
        database_opcodes = {instruction.opcode for instruction in database.instructions}
        self.assertEqual(set(by_opcode), database_opcodes | set(M9_OPCODE_ISSUES))

        slices = [slice_ for entry in entries for slice_ in entry["slices"]]
        self.assertEqual(len(slices), 98)
        self.assertEqual(len({slice_["id"] for slice_ in slices}), len(slices))
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
                "mul-mul-lo-u32", "mul-mul-rn-f32", "cvt-cvt-s32-u32",
                "cvt-cvt-rn-f32-f64", "cvt-cvt-rn-f32-u32", "cvt-cvt-rzi-u32-f32",
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
                "cvt-cvt-s32-u32": {"topology": "conversion", "types": ["s32", "u32"], "shape": "scalar"},
                "cvt-cvt-rn-f32-f64": {"topology": "conversion", "types": ["f32", "f64"], "shape": "scalar", "modifiers": ["rn"]},
                "cvt-cvt-rn-f32-u32": {"topology": "conversion", "types": ["f32", "u32"], "shape": "scalar", "modifiers": ["rn"]},
                "cvt-cvt-rzi-u32-f32": {"topology": "conversion", "types": ["u32", "f32"], "shape": "scalar", "modifiers": ["rzi"]},
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
                ("mov_pred", "default"),
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


if __name__ == "__main__":
    unittest.main()
