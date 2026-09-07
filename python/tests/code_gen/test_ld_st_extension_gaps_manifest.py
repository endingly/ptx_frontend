from pathlib import Path
import unittest

from jsonschema import Draft202012Validator

from ptx_frontend.code_gen.load_yaml import load_yaml


REPO_ROOT = Path(__file__).resolve().parents[3]
MANIFEST = REPO_ROOT / "instructions/ld_st_extension_gaps.yaml"
SCHEMA = REPO_ROOT / "instructions/schemas/ld-st-extension-gaps-v1.schema.yaml"


REQUIRED_GAPS = {
    "ld-l1-evict-priority-m10-i02": ("ld", "cache_eviction", "implemented"),
    "st-l1-evict-priority-m10-i02": ("st", "cache_eviction", "implemented"),
    "ld-l2-cache-hint-policy-m10-i02": ("ld", "cache_policy", "implemented"),
    "st-l2-cache-hint-policy-m10-i02": ("st", "cache_policy", "implemented"),
    "ld-l1-evict-priority-deferred": ("ld", "cache_eviction", "deferred"),
    "st-l1-evict-priority-deferred": ("st", "cache_eviction", "deferred"),
    "ld-l2-evict-priority-deferred": ("ld", "cache_eviction", "deferred"),
    "st-l2-evict-priority-deferred": ("st", "cache_eviction", "deferred"),
    "ld-l2-prefetch-size": ("ld", "prefetch", "deferred"),
    "ld-unified-address": ("ld", "address_qualifier", "deferred"),
    "ld-shared-subspace": ("ld", "state_space_subqualifier", "deferred"),
    "st-shared-subspace": ("st", "state_space_subqualifier", "deferred"),
    "ld-scalar-b128": ("ld", "scalar_type", "deferred"),
    "st-scalar-b128": ("st", "scalar_type", "deferred"),
    "ld-global-nc": ("ld", "opcode_form", "deferred"),
    "st-async": ("st", "opcode_form", "deferred"),
}


class LdStExtensionGapsManifestTests(unittest.TestCase):
    def test_schema_and_required_gap_inventory(self) -> None:
        manifest = load_yaml(MANIFEST)
        errors = sorted(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest),
            key=lambda error: list(error.path),
        )
        self.assertEqual(errors, [])

        gaps = manifest["gaps"]
        ids = [gap["id"] for gap in gaps]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual({gap["opcode"] for gap in gaps}, {"ld", "st"})

        inventory = {
            gap["id"]: (gap["opcode"], gap["kind"], gap["disposition"])
            for gap in gaps
        }
        self.assertEqual(inventory, REQUIRED_GAPS)

        m10_i02_ids = {
            gap["id"] for gap in gaps if gap["id"].endswith("-m10-i02")
        }
        self.assertTrue(m10_i02_ids)
        self.assertEqual(
            {gap["id"] for gap in gaps if gap["disposition"] == "implemented"},
            m10_i02_ids,
        )

        implemented = {
            gap["id"]: gap["implemented_contexts"]
            for gap in gaps
            if gap["disposition"] == "implemented"
        }
        self.assertEqual(
            implemented,
            {
                "ld-l1-evict-priority-m10-i02": [
                    {"state_space": "global", "vector": "scalar"}
                ],
                "st-l1-evict-priority-m10-i02": [
                    {"state_space": "global", "vector": "scalar"}
                ],
                "ld-l2-cache-hint-policy-m10-i02": [
                    {
                        "state_space": "global",
                        "vector": "scalar",
                        "cache_policy_operand": {
                            "required": True,
                            "width_bits": 64,
                        },
                    }
                ],
                "st-l2-cache-hint-policy-m10-i02": [
                    {
                        "state_space": "global",
                        "vector": "scalar",
                        "cache_policy_operand": {
                            "required": True,
                            "width_bits": 64,
                        },
                    }
                ],
            },
        )
        self.assertEqual(
            {
                gap["id"]: gap["deferred_contexts"]
                for gap in gaps
                if gap["disposition"] == "implemented"
            },
            {
                "ld-l1-evict-priority-m10-i02": [
                    "generic",
                    "vector",
                    "global_nc",
                ],
                "st-l1-evict-priority-m10-i02": [
                    "generic",
                    "vector",
                    "async",
                ],
                "ld-l2-cache-hint-policy-m10-i02": [
                    "generic",
                    "vector",
                    "global_nc",
                ],
                "st-l2-cache-hint-policy-m10-i02": [
                    "generic",
                    "vector",
                    "async",
                ],
            },
        )

    def test_implemented_gap_requires_structured_context(self) -> None:
        manifest = load_yaml(MANIFEST)
        del manifest["gaps"][0]["implemented_contexts"]
        errors = list(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest)
        )
        self.assertTrue(errors)

    def test_implemented_gap_requires_deferred_context(self) -> None:
        manifest = load_yaml(MANIFEST)
        del manifest["gaps"][0]["deferred_contexts"]
        errors = list(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest)
        )
        self.assertTrue(errors)

    def test_cache_policy_context_requires_64_bit_operand(self) -> None:
        manifest = load_yaml(MANIFEST)
        context = manifest["gaps"][2]["implemented_contexts"][0]
        context["cache_policy_operand"]["width_bits"] = 32
        errors = list(
            Draft202012Validator(load_yaml(SCHEMA)).iter_errors(manifest)
        )
        self.assertTrue(errors)

    def test_ld_global_nc_defers_only_residual_matrix_after_m12_i28(self) -> None:
        manifest = load_yaml(MANIFEST)
        gap = next(gap for gap in manifest["gaps"] if gap["id"] == "ld-global-nc")
        self.assertEqual(gap["disposition"], "deferred")
        self.assertIn("M12-I28", gap["boundary"])
        self.assertIn(
            "ld.global.nc.L1::no_allocate.u32 d, [a]", gap["boundary"]
        )


if __name__ == "__main__":
    unittest.main()
